"""Read SigmaTel logical drives from the identified Philips GoGear Mix.

No device-writing, erase, reset, or updater commands are implemented.
Protocol reference: rockbox/utils/imxtools/scsitools/stmp_scsi.{c,h}.
Windows transport reference: rockbox/utils/scsi/rbscsi.c.
"""

import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import sys
import traceback
import uuid

from probe_usb import GUID, Interface


ROOT = Path(__file__).resolve().parent
SERIAL = "020300014e144d3a0002d9b15f7bcd3a"


class ScsiPassThrough(c.Structure):
    _fields_ = [("length", w.USHORT), ("status", w.BYTE), ("path", w.BYTE),
                ("target", w.BYTE), ("lun", w.BYTE), ("cdb_length", w.BYTE),
                ("sense_length", w.BYTE), ("data_in", w.BYTE),
                ("transfer_length", w.ULONG), ("timeout", w.ULONG),
                ("data_offset", c.c_size_t), ("sense_offset", w.ULONG),
                ("cdb", w.BYTE * 16)]


def disk_interface():
    setup = c.WinDLL("setupapi", use_last_error=True)
    setup.SetupDiGetClassDevsW.argtypes = [c.POINTER(GUID), w.LPCWSTR, w.HWND, w.DWORD]
    setup.SetupDiGetClassDevsW.restype = c.c_void_p
    setup.SetupDiEnumDeviceInterfaces.argtypes = [c.c_void_p, c.c_void_p,
                                                 c.POINTER(GUID), w.DWORD,
                                                 c.POINTER(Interface)]
    setup.SetupDiGetDeviceInterfaceDetailW.argtypes = [c.c_void_p, c.POINTER(Interface),
                                                      c.c_void_p, w.DWORD,
                                                      c.POINTER(w.DWORD), c.c_void_p]
    setup.SetupDiDestroyDeviceInfoList.argtypes = [c.c_void_p]
    guid = GUID()
    c.memmove(c.byref(guid), uuid.UUID("53f56307-b6bf-11d0-94f2-00a0c91efb8b").bytes_le, 16)
    devices = setup.SetupDiGetClassDevsW(c.byref(guid), None, None, 0x12)
    if devices == c.c_void_p(-1).value:
        raise c.WinError(c.get_last_error())
    paths = []
    try:
        index = 0
        while True:
            interface = Interface()
            interface.size = c.sizeof(interface)
            if not setup.SetupDiEnumDeviceInterfaces(devices, None, c.byref(guid),
                                                     index, c.byref(interface)):
                if c.get_last_error() == 259:
                    break
                raise c.WinError(c.get_last_error())
            index += 1
            required = w.DWORD()
            setup.SetupDiGetDeviceInterfaceDetailW(devices, c.byref(interface), None,
                                                   0, c.byref(required), None)
            detail = c.create_string_buffer(required.value)
            c.cast(detail, c.POINTER(w.DWORD))[0] = 8 if c.sizeof(c.c_void_p) == 8 else 6
            if not setup.SetupDiGetDeviceInterfaceDetailW(devices, c.byref(interface),
                                                          detail, required, None, None):
                raise c.WinError(c.get_last_error())
            path = c.wstring_at(c.addressof(detail) + 4)
            if "disk&ven_philips&prod_gogear_mix" in path.lower() and SERIAL in path.lower():
                paths.append(path)
    finally:
        setup.SetupDiDestroyDeviceInfoList(devices)
    if len(paths) != 1:
        raise RuntimeError(f"Expected exactly one identified GoGear disk; found {len(paths)}")
    return paths[0]


class GoGear:
    def __init__(self):
        self.path = disk_interface()
        self.kernel = c.WinDLL("kernel32", use_last_error=True)
        self.kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p,
                                           w.DWORD, w.DWORD, w.HANDLE]
        self.kernel.CreateFileW.restype = w.HANDLE
        self.kernel.CloseHandle.argtypes = [w.HANDLE]
        self.kernel.DeviceIoControl.argtypes = [w.HANDLE, w.DWORD, c.c_void_p, w.DWORD,
                                               c.c_void_p, w.DWORD, c.POINTER(w.DWORD),
                                               c.c_void_p]
        # The Windows SCSI IOCTL requires read/write HANDLE access even for DATA_IN.
        # All commands issued below are restricted to documented reads.
        self.handle = self.kernel.CreateFileW(self.path, 0xC0000000, 3, None, 3, 0, None)
        if self.handle == c.c_void_p(-1).value:
            raise c.WinError(c.get_last_error())

    def close(self):
        self.kernel.CloseHandle(self.handle)

    def read(self, cdb, length):
        if not (len(cdb) == 6 and cdb[0] == 0x12) and not (
                len(cdb) == 16 and cdb[0] == 0xC0
                and cdb[1] in (0x00, 0x02, 0x05, 0x12, 0x13, 0x30, 0x37)):
            raise ValueError("Command is not in the read-only allowlist")
        if not 1 <= length <= 65536:
            raise ValueError("Invalid read size")
        header_size = c.sizeof(ScsiPassThrough)
        assert header_size == (56 if c.sizeof(c.c_void_p) == 8 else 44)
        sense_size = 32
        data_offset = (header_size + sense_size + 7) & ~7
        buffer = c.create_string_buffer(data_offset + length)
        packet = ScsiPassThrough.from_buffer(buffer)
        packet.length = header_size
        packet.cdb_length = len(cdb)
        packet.sense_length = sense_size
        packet.sense_offset = header_size
        packet.data_in = 1  # SCSI_IOCTL_DATA_IN, never DATA_OUT.
        packet.transfer_length = length
        packet.timeout = 5
        packet.data_offset = data_offset
        packet.cdb[:len(cdb)] = cdb
        returned = w.DWORD()
        if not self.kernel.DeviceIoControl(self.handle, 0x4D004, buffer, len(buffer),
                                           buffer, len(buffer), c.byref(returned), None):
            raise c.WinError(c.get_last_error())
        if packet.status:
            sense = buffer.raw[header_size:header_size + sense_size]
            raise RuntimeError(f"SCSI status 0x{packet.status:02x}, CDB={cdb.hex()}, "
                               f"sense={sense.hex()}")
        if packet.transfer_length > length:
            raise RuntimeError("Device returned an invalid transfer length")
        return buffer.raw[data_offset:data_offset + packet.transfer_length]

    def property(self, command, size, subcommand=0, field=0):
        cdb = bytearray(16)
        cdb[:4] = bytes((0xC0, command, subcommand, field))
        data = self.read(cdb, size)
        if len(data) != size:
            raise RuntimeError(f"Property 0x{command:02x} returned {len(data)} instead of {size}")
        return data

    def sectors(self, drive, address, count, sector_size):
        cdb = bytearray(16)
        cdb[:3] = bytes((0xC0, 0x13, drive))
        struct.pack_into(">QI", cdb, 3, address, count)
        data = self.read(cdb, count * sector_size)
        if len(data) != count * sector_size:
            raise RuntimeError(f"Short sector read from drive {drive}, address {address}")
        return data

    def info(self):
        inquiry = self.read(bytes((0x12, 0, 0, 0, 36, 0)), 36)
        if len(inquiry) != 36 or inquiry[8:16].strip() != b"Philips" or b"GoGear" not in inquiry[16:32]:
            raise RuntimeError("SCSI INQUIRY did not confirm Philips GoGear; refusing vendor reads")
        result = {"path": self.path, "serial": SERIAL,
                  "vendor": inquiry[8:16].decode().strip(),
                  "product": inquiry[16:32].decode().strip(),
                  "revision": inquiry[32:36].decode().strip()}
        print(json.dumps(result, indent=2), flush=True)
        result["protocol"] = self.property(0x00, 2).hex()
        count = int.from_bytes(self.property(0x05, 2), "big")
        if not 1 <= count <= 64:
            raise RuntimeError(f"Invalid logical-drive count: {count}")
        table = self.property(0x05, 2 + count * 11, count)
        if int.from_bytes(table[:2], "big") != count:
            raise RuntimeError("Logical-drive count changed")
        result["drives"] = []
        for index in range(count):
            drive, kind, tag, size = struct.unpack_from(">BBBQ", table, 2 + index * 11)
            entry = {"drive": drive, "type": kind, "tag": tag, "table_size": size}
            for key, field, width in (("sector_size", 0, 4), ("size", 2, 8),
                                      ("sector_count", 4, 8)):
                entry[key] = int.from_bytes(self.property(0x12, width, drive, field), "big")
            result["drives"].append(entry)
        return result


def dump_drive(device, entry, output):
    sector_size = entry["sector_size"]
    count = entry["sector_count"]
    total = sector_size * count
    if sector_size not in (512, 1024, 2048, 4096, 8192) or not 0 < total <= 256 * 1024 * 1024:
        raise RuntimeError(f"Unexpected system-drive geometry: {entry}")
    if entry["size"] != total or entry["table_size"] != total:
        raise RuntimeError(f"Inconsistent system-drive geometry: {entry}")
    path = output / f"drive_{entry['drive']:02x}_type_{entry['type']:02x}_tag_{entry['tag']:02x}.bin"
    digest = hashlib.sha256()
    batch_size = min(32, 65536 // sector_size)
    print(f"Reading drive {entry['drive']:02x}, tag {entry['tag']:02x}: {total} bytes", flush=True)
    with path.open("xb") as stream:
        for sector in range(0, count, batch_size):
            block = device.sectors(entry["drive"], sector, min(batch_size, count - sector), sector_size)
            stream.write(block)
            digest.update(block)
    # Read independently a second time to verify the complete backup.
    verification = hashlib.sha256()
    print("Verifying with a second complete read", flush=True)
    for sector in range(0, count, batch_size):
        verification.update(device.sectors(entry["drive"], sector,
                                            min(batch_size, count - sector), sector_size))
    if verification.digest() != digest.digest():
        raise RuntimeError(f"Verification mismatch for {path.name}; keep file for diagnosis")
    return {"file": path.name, "bytes": total, "sha256": digest.hexdigest(),
            "verified_second_read": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", action="store_true", help="Back up all non-user logical drives")
    args = parser.parse_args()
    device = GoGear()
    try:
        info = device.info()
        print(json.dumps(info, indent=2), flush=True)
        if not args.dump:
            return
        targets = [entry for entry in info["drives"] if entry["type"] in (1, 2)
                   and entry["tag"] not in (0x0A, 0x0B)]
        if not targets:
            raise RuntimeError("No system or hidden logical drives found")
        output = ROOT / "dumps" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        output.mkdir(parents=True, exist_ok=False)
        manifest = output / "manifest.json"
        info["backups"] = []
        manifest.write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
        print(f"Backup directory: {output}", flush=True)
        for entry in targets:
            info["backups"].append(dump_drive(device, entry, output))
            manifest.write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
        print("All selected logical drives were saved and verified twice.", flush=True)
    finally:
        device.close()


if __name__ == "__main__":
    class Tee:
        def __init__(self, console, log):
            self.console, self.log = console, log

        def write(self, value):
            self.console.write(value)
            self.log.write(value)
            self.log.flush()
            return len(value)

        def flush(self):
            self.console.flush()
            self.log.flush()

    with (ROOT / "gogear_firmware.log").open("a", encoding="utf-8") as log:
        original_stdout, original_stderr = sys.stdout, sys.stderr
        sys.stdout, sys.stderr = Tee(original_stdout, log), Tee(original_stderr, log)
        try:
            print("\nRun at " + datetime.now(timezone.utc).isoformat(), flush=True)
            main()
        except Exception:
            traceback.print_exc()
            raise SystemExit(1)
        finally:
            sys.stdout, sys.stderr = original_stdout, original_stderr
