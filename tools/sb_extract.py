#!/usr/bin/env python3
"""Inspect/extract STMP SB 1.1 images; stdlib + openssl, no target execution.

Format reference: Rockbox utils/imxtools/sbtools/{sb.h,sb.c,crc.c}.
Only the standard all-zero wrapping key is tried. Each CALL/JUMP ends a
separate ELF load group so successive boot stages never overwrite one another.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


def aes(data, key, iv, decrypt=False):
    args = ['openssl', 'enc', '-aes-128-cbc', '-nopad', '-K', key.hex(),
            '-iv', iv.hex()]
    if decrypt:
        args.append('-d')
    return subprocess.run(args, input=data, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=True).stdout


def require(condition, message):
    if not condition:
        raise ValueError(message)


def crc32_mpeg(data):
    table = []
    for value in range(256):
        value <<= 24
        for _ in range(8):
            value = ((value << 1) ^ (0x04c11db7 if value & 0x80000000 else 0)) & 0xffffffff
        table.append(value)
    value = 0xffffffff
    for byte in data:
        value = ((value << 8) & 0xffffffff) ^ table[(value >> 24) ^ byte]
    return value


def elf32(segments, entry):
    # ELF32 little-endian ARM, one PT_LOAD per LOAD/FILL; no invented sections.
    ident = b'\x7fELF\x01\x01\x01' + bytes(9)
    out = bytearray(ident + struct.pack('<HHIIIIIHHHHHH', 2, 40, 1, entry,
                    52, 0, 0, 52, 32, len(segments), 0, 0, 0))
    offset = 52 + 32 * len(segments)
    payload = bytearray()
    for address, data, size in segments:
        out += struct.pack('<8I', 1, offset, address, address, len(data), size, 7, 1)
        payload += data
        offset += len(data)
    return out + payload


def extract(source, destination):
    p = source.read_bytes()
    require(len(p) >= 96 and p[20:24] == b'STMP', 'Not an STMP SB image')
    require(p[24:26] == b'\x01\x01', 'Only SB 1.1 supported')
    require(hashlib.sha1(p[20:96]).digest() == p[:20], 'Header SHA-1 mismatch')
    size = struct.unpack_from('<I', p, 28)[0] * 16
    require(size == len(p), 'Image size mismatch (supply the exact SB container)')
    keys, dictionary, header_blocks, count, section_blocks = struct.unpack_from('<5H', p, 40)
    require(header_blocks == 6 and section_blocks == 1, 'Unexpected header layout')
    require(keys == 1 and dictionary == 6 + count, 'Only one-key SB dictionaries supported')
    dictionary *= 16
    require(dictionary + 32 <= len(p), 'Truncated key dictionary')
    mac = aes(p[:dictionary], bytes(16), bytes(16))[-16:]
    require(mac == p[dictionary:dictionary+16], 'Zero wrapping key did not match CBC-MAC')
    key = aes(p[dictionary+16:dictionary+32], bytes(16), p[:16], True)
    trailer = aes(p[-32:], key, p[:16], True)
    require(trailer[:20] == hashlib.sha1(p[:-32]).digest(), 'Full image SHA-1 mismatch')
    destination.mkdir(parents=True, exist_ok=False)
    report = dict(source=str(source.resolve()), sha256=hashlib.sha256(p).hexdigest(),
                  image_bytes=size, header_sha1_valid=True, zero_key_cbc_mac_valid=True,
                  image_sha1_valid=True, timestamp=str(datetime.datetime(2000, 1, 1) +
                  datetime.timedelta(microseconds=struct.unpack_from('<Q', p, 56)[0])),
                  sections=[])
    for index in range(count):
        ident, offset, length, flags = struct.unpack_from('<4I', p, 96+16*index)
        offset *= 16
        length *= 16
        require(dictionary+32 <= offset <= offset+length <= len(p)-32, 'Invalid section range')
        name = ident.to_bytes(4, 'big').decode('ascii') if ident else 'boot'
        require(re.fullmatch('[A-Za-z0-9_]+', name), 'Unsafe section name')
        data = p[offset:offset+length]
        encrypted = not flags & 2
        if encrypted:
            data = aes(data, key, p[:16], True)
        stem = f'{index:02}_{name}'
        (destination / f'{stem}.bin').write_bytes(data)
        section = dict(name=name, identifier=f'0x{ident:08x}', file_offset=offset,
                       size=length, flags=flags, encrypted=encrypted,
                       sha256=hashlib.sha256(data).hexdigest(), commands=[])
        strings = []
        for match in re.finditer(rb'[\x20-\x7e]{6,}', data):
            strings.append(f'{match.start():08x}\t{match.group().decode("ascii")}\n')
        (destination / f'{stem}.strings.tsv').write_text(''.join(strings))
        if flags & 1:
            pos, group, segments = 0, 0, []
            while pos < len(data):
                require(pos+16 <= len(data), 'Truncated command')
                checksum, opcode, cmdflags, address, n, value = struct.unpack_from('<BBHIII', data, pos)
                require(checksum == (90+sum(data[pos+1:pos+16])) & 255, 'Command checksum mismatch')
                require(opcode in (0, 2, 3, 4, 5, 6), 'Unsupported command')
                cmd = dict(offset=pos, opcode=opcode, flags=cmdflags,
                           address=f'0x{address:08x}', length=n, value=f'0x{value:08x}')
                pos += 16
                if opcode == 2:
                    padded = (n+15)//16*16
                    require(pos+padded <= len(data), 'Truncated LOAD')
                    require(crc32_mpeg(data[pos:pos+padded]) == value, 'LOAD CRC mismatch')
                    cmd['crc_valid'] = True
                    segments.append((address, data[pos:pos+n], n))
                    pos += padded
                elif opcode == 3:
                    require(cmdflags in (0, 1, 2) and n <= 16*1024*1024, 'Unsupported FILL')
                    width = 1 << cmdflags
                    pattern = value.to_bytes(4, 'little')[:width]
                    filled = (pattern * ((n+width-1)//width))[:n] if value else b''
                    segments.append((address, filled, n))
                elif opcode in (4, 5):
                    filename = f'{stem}_stage_{group:02}.elf'
                    (destination / filename).write_bytes(elf32(segments, address))
                    cmd['elf'] = filename
                    segments = []
                    group += 1
                section['commands'].append(cmd)
            require(not segments, 'Unterminated load group')
        report['sections'].append(section)
    (destination / 'manifest.json').write_text(json.dumps(report, indent=2)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path, help='New output directory')
    args = parser.parse_args()
    result = extract(args.source, args.destination)
    print(json.dumps(result, indent=2))
