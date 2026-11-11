#!/usr/bin/env python3
"""Extract the separate USB-host UI bitmap table embedded inside HVMI.

Observed: FUN_0010565c indexes 48 pointers at VA 0x10c054, with HVMI mapped
at 0xea000. This is independent of the shared RSRC image tables.
"""
import hashlib
import json
from pathlib import Path
import runpy
import struct

ROOT = Path(__file__).resolve().parents[1]


def read_host_assets(root=ROOT):
    source = root / 'gen/sb/03_hvmi.bin'
    data = source.read_bytes()
    manifest = json.loads((root / 'gen/sb/manifest.json').read_text())
    assert hashlib.sha256(data).hexdigest() == manifest['sections'][3]['sha256']
    base, table = 0xea000, 0x10c054
    rows = []
    for index in range(48):
        address = struct.unpack_from('<I', data, table - base + index*4)[0]
        offset = address - base
        assert 0 <= offset <= len(data)-4
        header = struct.unpack_from('<I', data, offset)[0]
        mode, width, height = header & 255, (header >> 8) & 4095, header >> 20
        assert mode in (0, 1) and 0 < width <= 128 and 0 < height <= 64
        size = width*height*2 if mode == 0 else ((width+7)//8)*height
        payload = data[offset+4:offset+4+size]
        assert len(payload) == size
        if mode == 0:
            pixels = list(struct.unpack('<'+'H'*(width*height), payload))
        else:
            stride = (width+7)//8
            pixels = [65535 if payload[y*stride+x//8] & (0x80 >> (x%8)) else 0
                      for y in range(height) for x in range(width)]
        rows.append((dict(index=index, address=hex(address), hvmi_offset=hex(offset),
                          mode=mode, width=width, height=height,
                          payload_sha256=hashlib.sha256(data[offset:offset+4+size]).hexdigest()), pixels))
    return rows


def extract():
    write_png = runpy.run_path(str(ROOT / 'tools/pngw.py'))['write']
    destination = ROOT / 'gen/host-ui'
    destination.mkdir(parents=True, exist_ok=True)
    assets = read_host_assets()
    for row, pixels in assets:
        w, h = row['width'], row['height']
        rgb = bytes(c for p in pixels for c in (((p >> 11) & 31)*255//31,
                    ((p >> 5) & 63)*255//63, (p & 31)*255//31))
        write_png(destination / f'{row["index"]:02}.png', w, h,
                  [rgb[y*w*3:(y+1)*w*3] for y in range(h)], 2)
    report = {'source': 'gen/sb/03_hvmi.bin', 'vmi_base': '0xea000',
              'table_address': '0x10c054', 'images': [row for row, _ in assets]}
    (destination / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Extracted {len(assets)} host-mode images to {destination}')


if __name__ == '__main__':
    extract()
