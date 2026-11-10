"""Verify the saved GoGear backups and extract their exact boot-image container."""

import argparse
import hashlib
import json
from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parent


def inspect_image(data):
    if len(data) < 96 or data[20:24] != b"STMP":
        raise RuntimeError("No STMP boot-image header at the beginning of the system drive")
    rom, size, header_size, userdata = struct.unpack_from("<4I", data)
    if header_size == 52 and 52 <= size <= len(data):
        return {"format": "SB1", "extension": ".sb1", "bytes": size,
                "rom_version": hex(rom), "userdata_offset": userdata,
                "header_hex": data[:52].hex()}
    major, minor = data[24:26]
    image_size = struct.unpack_from("<I", data, 28)[0] * 16
    header_size = struct.unpack_from("<H", data, 44)[0] * 16
    if major != 1 or minor not in (0, 1) or header_size != 96 or not 96 <= image_size <= len(data):
        raise RuntimeError("Unrecognized STMP boot-image format")
    if hashlib.sha1(data[20:96]).digest() != data[:20]:
        raise RuntimeError("Firmware header SHA-1 does not match")
    return {"format": "SB2", "extension": ".sb", "bytes": image_size,
            "header_version": f"{major}.{minor}", "header_sha1_valid": True,
            "product_version_fields": struct.unpack_from(">H2xH2xH2x", data, 64),
            "component_version_fields": struct.unpack_from(">H2xH2xH2x", data, 76)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--extract", action="store_true")
    args = parser.parse_args()
    directory = args.directory.resolve()
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    saved = {}
    for entry in manifest["backups"]:
        if Path(entry["file"]).name != entry["file"]:
            raise RuntimeError("Invalid backup filename")
        data = (directory / entry["file"]).read_bytes()
        if len(data) != entry["bytes"] or hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise RuntimeError("Local backup verification failed: " + entry["file"])
        if not entry["verified_second_read"]:
            raise RuntimeError("Backup was not verified against a second device read")
        saved[entry["file"]] = data
        print(f"VERIFIED {entry['file']}: {len(data)} bytes, {entry['sha256']}")
    system_files = [name for name in saved if "_type_01_" in name]
    if not system_files:
        raise RuntimeError("No system-drive backups")
    same = all(saved[name] == saved[system_files[0]] for name in system_files)
    print(f"System partitions identical: {same}")
    boot_files = [name for name in system_files if name.endswith("_tag_50.bin")]
    if len(boot_files) != 1:
        raise RuntimeError("Expected exactly one boot-drive backup")
    data = saved[boot_files[0]]
    info = inspect_image(data)
    image = data[:info["bytes"]]
    info["sha256"] = hashlib.sha256(image).hexdigest()
    info["source"] = boot_files[0]
    info["system_copies_identical"] = same
    print(json.dumps(info, indent=2))
    if args.extract:
        output = directory / ("firmware" + info["extension"])
        with output.open("xb") as stream:
            stream.write(image)
        (directory / "firmware_verification.json").write_text(
            json.dumps(info, indent=2) + "\n", encoding="utf-8")
        print(f"EXTRACTED {output}")


if __name__ == "__main__":
    main()
