#!/usr/bin/env python3
"""Inventory clear STMP RSRC, export UTF-16 text and small UI RGB565 images.

Run with python3 -I. PNG output uses only stdlib. Format reference:
Rockbox utils/imxtools/sbtools/rsrc.{h,c}. Pixel header interpretation is
derived from GoGear Mix resource dimensions and visually checked output.
Glyph/font tables stay indexed in the inventory rather than becoming files.
"""
import argparse
import collections
import json
from pathlib import Path
import struct
import zlib


def png(path, width, height, pixels):
    def chunk(tag, data):
        return struct.pack('>I', len(data))+tag+data+struct.pack('>I', zlib.crc32(tag+data))
    raw = b''.join(b'\0'+pixels[y*width*3:(y+1)*width*3] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB',
                     width, height, 8, 2, 0, 0, 0))+chunk(b'IDAT', zlib.compress(raw))+
                     chunk(b'IEND', b''))


def extract(source, destination):
    data = source.read_bytes()
    if data[20:24] != b'RSRC':
        raise ValueError('Missing RSRC marker')
    entries, tables = {}, set()

    def walk(offset, path):
        if offset in tables:
            return
        if len(path) > 3 or offset+1024 > len(data):
            raise ValueError('Invalid table range/depth')
        tables.add(offset)
        for index in range(256):
            value = struct.unpack_from('<I', data, offset+4*index)[0]
            kind, pos = value >> 28, value & 0xfffffff
            ids = path+[index]
            if kind == 0:
                continue
            if kind == 1:
                walk(pos, ids)
            elif kind != 3:
                if kind not in (2, 4, 5) or pos+4 > len(data):
                    raise ValueError('Invalid resource type/range')
                size = struct.unpack_from('<I', data, pos)[0]
                if pos+4+size > len(data):
                    raise ValueError('Truncated resource')
                entries.setdefault((kind, pos+4), (ids, kind, pos+4, size))

    walk(2048, [])
    destination.mkdir(parents=True, exist_ok=False)
    images = destination / 'images'
    images.mkdir()
    text_rows, image_rows = [], []
    with (destination / 'inventory.tsv').open('w') as out:
        out.write('path\ttype\toffset\tsize\n')
        for ids, kind, offset, size in entries.values():
            label = '.'.join(map(str, ids))
            out.write(f'{label}\t{kind}\t0x{offset:x}\t{size}\n')
            payload = data[offset:offset+size]
            if kind == 5 and ids[0] in (10, 11) and size % 2 == 0:
                try:
                    text = payload.decode('utf-16le').rstrip('\0')
                except UnicodeDecodeError:
                    continue
                text_rows.append(f'{label}\t0x{offset:x}\t{json.dumps(text, ensure_ascii=False)}\n')
            if kind == 2 and ids[0] < 10 and size >= 4:
                header = struct.unpack_from('<I', payload)[0]
                width, height = (header >> 8) & 4095, header >> 20
                if header & 255 or not width or not height or size != 4+2*width*height:
                    continue
                rgb = bytearray()
                for pixel, in struct.iter_unpack('<H', payload[4:]):
                    rgb.extend((((pixel >> 11) & 31)*255//31,
                                ((pixel >> 5) & 63)*255//63, (pixel & 31)*255//31))
                filename = label+'.png'
                png(images / filename, width, height, rgb)
                image_rows.append(dict(path=label, offset=offset, width=width,
                                       height=height, file='images/'+filename))
    (destination / 'texts.tsv').write_text('path\toffset\ttext_json\n'+''.join(text_rows))
    report = dict(source=str(source.resolve()), unique_tables=len(tables),
                  unique_payloads_by_type=dict(collections.Counter(row[1] for row in entries.values())),
                  decoded_texts=len(text_rows), decoded_ui_images=len(image_rows), images=image_rows,
                  limitations=['One path per shared table/payload retained; offsets refer to RSRC section.',
                               'Value entries omitted; glyph/font images indexed but not exported.',
                               'RGB565 interpretation derived from dimensions and visual inspection.'])
    (destination / 'manifest.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k != 'images'}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path, help='New output directory')
    args = parser.parse_args()
    extract(args.source, args.destination)
