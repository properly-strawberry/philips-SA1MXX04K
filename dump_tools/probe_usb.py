"""Read Windows metadata for the GoGear recovery interface; never write to it."""

import ctypes as c
from ctypes import wintypes as w
import importlib.util
import json
import subprocess
import uuid


class GUID(c.Structure):
    _fields_ = [("bytes", c.c_ubyte * 16)]


class Interface(c.Structure):
    _fields_ = [("size", w.DWORD), ("guid", GUID), ("flags", w.DWORD),
                ("reserved", c.c_void_p)]


class Attributes(c.Structure):
    _fields_ = [("size", w.ULONG), ("vendor", w.USHORT),
                ("product", w.USHORT), ("version", w.USHORT)]


class HidCaps(c.Structure):
    _fields_ = [(name, w.USHORT) for name in
                ("usage", "usage_page", "input_length", "output_length", "feature_length")]
    _fields_ += [("reserved", w.USHORT * 17)]
    _fields_ += [(name, w.USHORT) for name in
                 ("link_nodes", "input_buttons", "input_values", "input_indices",
                  "output_buttons", "output_values", "output_indices",
                  "feature_buttons", "feature_values", "feature_indices")]


def report_caps(handle):
    hid = c.WinDLL("hid", use_last_error=True)
    hid.HidD_GetPreparsedData.argtypes = [w.HANDLE, c.POINTER(c.c_void_p)]
    hid.HidD_GetPreparsedData.restype = c.c_ubyte
    hid.HidD_FreePreparsedData.argtypes = [c.c_void_p]
    hid.HidP_GetCaps.argtypes = [c.c_void_p, c.POINTER(HidCaps)]
    hid.HidP_GetValueCaps.argtypes = [c.c_int, c.c_void_p, c.POINTER(w.USHORT), c.c_void_p]
    preparsed = c.c_void_p()
    if not hid.HidD_GetPreparsedData(handle, c.byref(preparsed)):
        raise c.WinError(c.get_last_error())
    try:
        caps = HidCaps()
        if hid.HidP_GetCaps(preparsed, c.byref(caps)) != 0x110000:
            raise RuntimeError("HidP_GetCaps failed")
        result = {name: getattr(caps, name) for name in
                  ("usage", "usage_page", "input_length", "output_length", "feature_length")}
        for kind, name in enumerate(("input", "output", "feature")):
            count = w.USHORT(getattr(caps, name + "_values"))
            if not count.value:
                continue
            values = c.create_string_buffer(count.value * 72)
            if hid.HidP_GetValueCaps(kind, values, c.byref(count), preparsed) != 0x110000:
                raise RuntimeError("HidP_GetValueCaps failed")
            import struct
            result[name + "_reports"] = []
            for index in range(count.value):
                value = values.raw[index * 72:(index + 1) * 72]
                bits, items = struct.unpack_from("<HH", value, 18)
                result[name + "_reports"].append({"id": value[2], "bits": bits,
                                                   "count": items})
        return result
    finally:
        hid.HidD_FreePreparsedData(preparsed)


def hid_descriptors():
    setup = c.WinDLL("setupapi", use_last_error=True)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    hid = c.WinDLL("hid", use_last_error=True)
    setup.SetupDiGetClassDevsW.argtypes = [c.POINTER(GUID), w.LPCWSTR, w.HWND, w.DWORD]
    setup.SetupDiGetClassDevsW.restype = c.c_void_p
    setup.SetupDiEnumDeviceInterfaces.argtypes = [c.c_void_p, c.c_void_p,
                                                 c.POINTER(GUID), w.DWORD,
                                                 c.POINTER(Interface)]
    setup.SetupDiGetDeviceInterfaceDetailW.argtypes = [c.c_void_p, c.POINTER(Interface),
                                                      c.c_void_p, w.DWORD,
                                                      c.POINTER(w.DWORD), c.c_void_p]
    setup.SetupDiDestroyDeviceInfoList.argtypes = [c.c_void_p]
    kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p,
                                  w.DWORD, w.DWORD, w.HANDLE]
    kernel.CreateFileW.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    hid.HidD_GetAttributes.argtypes = [w.HANDLE, c.POINTER(Attributes)]
    for name in ("HidD_GetManufacturerString", "HidD_GetProductString",
                 "HidD_GetSerialNumberString"):
        getattr(hid, name).argtypes = [w.HANDLE, c.c_void_p, w.ULONG]

    guid = GUID()
    c.memmove(c.byref(guid), uuid.UUID("4d1e55b2-f16f-11cf-88cb-001111000030").bytes_le, 16)
    devices = setup.SetupDiGetClassDevsW(c.byref(guid), None, None, 0x12)
    invalid = c.c_void_p(-1).value
    if devices == invalid:
        raise c.WinError(c.get_last_error())
    found = []
    try:
        index = 0
        while True:
            interface = Interface()
            interface.size = c.sizeof(interface)
            if not setup.SetupDiEnumDeviceInterfaces(devices, None, c.byref(guid),
                                                     index, c.byref(interface)):
                error = c.get_last_error()
                if error == 259:
                    break
                raise c.WinError(error)
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
            if "vid_066f" not in path.lower():
                continue
            # Zero requested access: descriptor queries only; no reports or payloads.
            handle = kernel.CreateFileW(path, 0, 3, None, 3, 0, None)
            item = {"path": path}
            if handle == invalid:
                item["open_error"] = c.get_last_error()
                found.append(item)
                continue
            try:
                attrs = Attributes()
                attrs.size = c.sizeof(attrs)
                if hid.HidD_GetAttributes(handle, c.byref(attrs)):
                    item.update(vendor=f"{attrs.vendor:04x}", product=f"{attrs.product:04x}",
                                version=f"{attrs.version:04x}")
                item["reports"] = report_caps(handle)
                for key, name in (("manufacturer", "HidD_GetManufacturerString"),
                                  ("product_name", "HidD_GetProductString"),
                                  ("serial", "HidD_GetSerialNumberString")):
                    buffer = c.create_unicode_buffer(256)
                    if getattr(hid, name)(handle, buffer, c.sizeof(buffer)):
                        item[key] = buffer.value
                found.append(item)
            finally:
                kernel.CloseHandle(handle)
    finally:
        setup.SetupDiDestroyDeviceInfoList(devices)
    return found


if __name__ == "__main__":
    print(json.dumps({"hid_descriptors": hid_descriptors(),
                      "installed_modules": {name: importlib.util.find_spec(name) is not None
                                            for name in ("hid", "hidapi", "usb")}}, indent=2))
    result = subprocess.run(["pnputil", "/enum-devices", "/connected", "/bus", "USB",
                             "/properties", "/relations"], capture_output=True)
    output = result.stdout.decode("utf-8", errors="replace")
    import re
    for block in re.split(r"(?=^Instance ID:)", output, flags=re.MULTILINE):
        if re.match(r"Instance ID:\s+USB\\VID_066F", block, flags=re.IGNORECASE):
            print(block)
    if result.returncode:
        raise SystemExit(result.returncode)
