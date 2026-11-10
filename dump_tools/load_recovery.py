"""Load the reviewed Rockbox helper into STMP3700 RAM using Windows HID.

Protocol reference: rockbox/utils/imxtools/sbtools/sbloader.c (GPL-2.0-or-later).
Only the pinned, locally built SB1 helper is accepted. No flash commands exist here.
Running without --send only inspects the image and HID device.
"""

import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import struct

from probe_usb import hid_descriptors, report_caps


ROOT = Path(__file__).resolve().parent
IMAGE = ROOT / "rockbox/utils/hwstub/stub/stmp/build/hwstub.sb1"
EXPECTED_SHA256 = "f818bfae06e439156dc0bdaac8a542a4bf859c02703de6fda524076b2ddcc512"
TAG = 0xCAFEBABE


class Overlapped(c.Structure):
    _fields_ = [("internal", c.c_size_t), ("internal_high", c.c_size_t),
                ("offset", w.DWORD), ("offset_high", w.DWORD), ("event", w.HANDLE)]


def checked_image():
    data = IMAGE.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise RuntimeError("Helper hash differs from the reviewed build; refusing transfer")
    rom, size, header, userdata = struct.unpack_from("<4I", data)
    if data[20:24] != b"STMP" or size != len(data) or header != 52 or userdata != 0:
        raise RuntimeError("Unexpected SB1 header")
    if len(data) % 512 or not 512 <= len(data) <= 32768:
        raise RuntimeError("Unexpected helper size")
    print(json.dumps({"image": str(IMAGE), "bytes": size, "sha256": digest,
                      "rom_version": hex(rom), "format": "SB1"}, indent=2), flush=True)
    return data


def windows_api():
    kernel = c.WinDLL("kernel32", use_last_error=True)
    hid = c.WinDLL("hid", use_last_error=True)
    kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p,
                                  w.DWORD, w.DWORD, w.HANDLE]
    kernel.CreateFileW.restype = w.HANDLE
    kernel.CreateEventW.argtypes = [c.c_void_p, w.BOOL, w.BOOL, w.LPCWSTR]
    kernel.CreateEventW.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.ReadFile.argtypes = [w.HANDLE, c.c_void_p, w.DWORD, c.POINTER(w.DWORD),
                               c.POINTER(Overlapped)]
    kernel.GetOverlappedResult.argtypes = [w.HANDLE, c.POINTER(Overlapped),
                                          c.POINTER(w.DWORD), w.BOOL]
    kernel.CancelIoEx.argtypes = [w.HANDLE, c.POINTER(Overlapped)]
    kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
    kernel.WaitForSingleObject.restype = w.DWORD
    hid.HidD_SetOutputReport.argtypes = [w.HANDLE, c.c_void_p, w.ULONG]
    hid.HidD_SetOutputReport.restype = c.c_ubyte
    return kernel, hid


def send_helper(device, data):
    kernel, hid = windows_api()
    handle = kernel.CreateFileW(device["path"], 0xC0000000, 3, None, 3, 0x40000000, None)
    if handle == c.c_void_p(-1).value:
        raise c.WinError(c.get_last_error())
    event = None
    pending = False
    operation = Overlapped()
    try:
        caps = report_caps(handle)
        if caps["output_length"] != 65 or caps["input_length"] != 65:
            raise RuntimeError("Unexpected HID transfer lengths; refusing transfer")
        event = kernel.CreateEventW(None, True, False, None)
        if not event:
            raise c.WinError(c.get_last_error())
        operation.event = event
        status = c.create_string_buffer(caps["input_length"])
        received = w.DWORD()
        if not kernel.ReadFile(handle, status, len(status), c.byref(received), c.byref(operation)):
            error = c.get_last_error()
            if error != 997:  # ERROR_IO_PENDING
                raise c.WinError(error)
            pending = True

        def output(report_id, payload):
            if len(payload) > 64:
                raise ValueError("Oversized HID report")
            report = bytes([report_id]) + payload.ljust(64, b"\xff")
            buffer = c.create_string_buffer(report, len(report))
            if not hid.HidD_SetOutputReport(handle, buffer, len(buffer)):
                raise c.WinError(c.get_last_error())

        # BLTC command wrapper: command 2 downloads a boot image into RAM.
        # Wrapper fields are little-endian; the CDB's image length is big-endian.
        command = (b"BLTC" + struct.pack("<II", TAG, len(data)) + b"\x00\x00\x00"
                   + b"\x02" + struct.pack(">I", len(data)) + bytes(11))
        assert len(command) == 31
        print("Sending BLTC RAM download command", flush=True)
        output(1, command)
        for offset in range(0, len(data), 64):
            output(2, data[offset:offset + 64])
        print(f"Sent {len(data)} bytes; waiting for ROM status", flush=True)
        if pending:
            waited = kernel.WaitForSingleObject(event, 5000)
            if waited == 258:
                print("No ROM status within 5 seconds; verify USB re-enumeration.", flush=True)
                return
            if waited != 0:
                raise c.WinError(c.get_last_error())
            if not kernel.GetOverlappedResult(handle, c.byref(operation), c.byref(received), False):
                raise c.WinError(c.get_last_error())
            pending = False
        packet = status.raw[:received.value]
        print("ROM response: " + packet.hex(), flush=True)
        if len(packet) < 14:
            raise RuntimeError("Short ROM status")
        report_id, signature, tag, residue, result = struct.unpack_from("<B4sIIB", packet)
        if report_id != 4 or signature != b"BLTS" or tag != TAG:
            raise RuntimeError("Unrecognized ROM status")
        if residue or result:
            raise RuntimeError(f"ROM rejected helper: status={result}, residue={residue}")
        print("ROM accepted RAM helper. Check for USB VID_FEE1&PID_DEAD.", flush=True)
    finally:
        if pending:
            kernel.CancelIoEx(handle, c.byref(operation))
            kernel.GetOverlappedResult(handle, c.byref(operation), c.byref(received), True)
        kernel.CloseHandle(handle)
        if event:
            kernel.CloseHandle(event)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--send", action="store_true", help="Send the pinned RAM helper once")
    args = parser.parse_args()
    image = checked_image()
    devices = [device for device in hid_descriptors()
               if device.get("vendor") == "066f" and device.get("product") == "3700"
               and device.get("product_name") == "ROM Recovery"]
    if len(devices) != 1:
        raise SystemExit(f"Expected one STMP3700 ROM Recovery device, found {len(devices)}")
    print(json.dumps(devices[0], indent=2), flush=True)
    if args.send:
        send_helper(devices[0], image)
    else:
        print("Inspection complete; no reports sent.")
