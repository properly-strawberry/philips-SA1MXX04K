#!/usr/bin/env python3
"""Embed the original GoGear RGB565 UI assets and Unicode bitmap font."""
from pathlib import Path
import struct
import json
import runpy

root = Path(__file__).resolve().parents[1]
source = root / 'gen/sb/05_rsrc.bin'
inventory = root / 'gen/resources/inventory.tsv'
data = source.read_bytes()
pixels, bits, images, glyphs, large_glyphs, time_glyphs, counter_glyphs, dial_glyphs = [], bytearray(), [], [], [], [], [], []
for row in inventory.read_text().splitlines()[1:]:
    name, kind, offset, size = row.split('\t')
    ids = list(map(int, name.split('.')))
    if kind != '2':
        continue
    off, size = int(offset, 16), int(size)
    header = struct.unpack_from('<I', data, off)[0]
    w, h, mode = (header >> 8) & 4095, header >> 20, header & 255
    if ids[0] < 10 and mode == 0 and size == 4+2*w*h:
        images.append((ids[0]*256+ids[1], w, h, len(pixels)))
        pixels.extend(struct.unpack_from(f'<{w*h}H', data, off+4))
    elif ids[0] in (33, 34) and len(ids) == 3 and mode == 1 and size == 4+((w+7)//8)*h:
        (glyphs if ids[0] == 33 else large_glyphs).append((ids[1]*256+ids[2], w, h, len(bits)))
        bits.extend(data[off+4:off+size])
    elif ids[0] in (30, 31, 32) and len(ids) == 3 and mode == 1 and size == 4+((w+7)//8)*h:
        {30:time_glyphs,31:dial_glyphs,32:counter_glyphs}[ids[0]].append((ids[1]*256+ids[2], w, h, len(bits)))
        bits.extend(data[off+4:off+size])
# Desktop-only 0x10000 namespace for the independent HVMI bitmap pointer table.
host_assets = runpy.run_path(str(root/'tools/extract_host_ui.py'))['read_host_assets'](root)
for row, values in host_assets:
    images.append((0x10000+row['index'], row['width'], row['height'], len(pixels)))
    pixels.extend(values)
out = root / 'gen/player/assets.h'
out.parent.mkdir(parents=True, exist_ok=True)
with out.open('w') as f:
    f.write('// Generated from the supplied firmware. Do not edit.\n#pragma once\n#include <cstdint>\n')
    f.write('struct Asset { unsigned id, w, h, offset; };\n')
    for name, rows in [('assets', images), ('glyphs', sorted(glyphs)), ('largeGlyphs', sorted(large_glyphs)), ('timeGlyphs', sorted(time_glyphs)), ('counterGlyphs', sorted(counter_glyphs)), ('dialGlyphs', sorted(dial_glyphs))]:
        f.write(f'inline constexpr Asset {name}[] = {{\n')
        for row in rows:
            f.write('{'+','.join(map(str,row))+'},\n')
        f.write('};\n')
    texts, legal = {}, {}
    for row in (root/'gen/resources/texts.tsv').read_text().splitlines()[1:]:
        resource, _, value = row.split('\t', 2)
        ids = list(map(int, resource.split('.')))
        if ids[0] == 10:
            texts[(ids[1], ids[2])] = json.loads(value).replace('\0', '')
        elif ids[:2] == [11, 0] and len(ids) == 3:
            line = json.loads(value).replace('\0', '').replace('\uffff', '')
            if line:
                legal[ids[2]] = line
    f.write('inline constexpr const wchar_t* firmwareText[24][235] = {\n')
    for language in range(24):
        f.write('{\n')
        for index in range(235):
            value = texts.get((language,index), texts.get((2,index), ''))
            # Numeric escapes keep the generated header portable across compilers.
            units = struct.unpack('<'+'H'*(len(value.encode('utf-16le'))//2),value.encode('utf-16le'))
            f.write('L"'+''.join(f'\\x{unit:04x}'+'" L"' for unit in units)+'",\n')
        f.write('},\n')
    f.write('};\n')
    f.write('inline constexpr const wchar_t* firmwareLegal[] = {\n')
    for _, value in sorted(legal.items()):
        units = struct.unpack('<'+'H'*(len(value.encode('utf-16le'))//2), value.encode('utf-16le'))
        f.write('L"'+''.join(f'\\x{unit:04x}'+'" L"' for unit in units)+'",\n')
    f.write('};\n')
    for typ, name, values in [('uint16_t','assetPixels',pixels),('uint8_t','fontBits',bits)]:
        f.write(f'inline constexpr {typ} {name}[] = {{\n')
        for i in range(0,len(values),32):
            f.write(','.join(map(str,values[i:i+32]))+',\n')
        f.write('};\n')
print(f'Embedded {len(images)} UI images, {len(glyphs)} small and {len(large_glyphs)} large glyphs, 24 text tables into {out}')
