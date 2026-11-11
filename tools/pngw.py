"""Minimal stdlib PNG writer: 8-bit indexed (PLTE + optional tRNS), 8/16-bit
grayscale, 8-bit RGB/RGBA. Rows are given top-down as bytes objects."""
import struct
import zlib


def _chunk(tag, data):
    c = struct.pack('>I', len(data)) + tag + data
    return c + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)


def write(path, width, height, rows, color_type, bit_depth=8, palette=None, trns=None, text=None):
    """color_type: 0 gray, 2 RGB, 3 indexed, 6 RGBA. rows: list of bytes, one per row."""
    raw = b''.join(b'\0' + r for r in rows)
    out = [b'\x89PNG\r\n\x1a\n',
           _chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, bit_depth, color_type, 0, 0, 0))]
    if palette is not None:
        out.append(_chunk(b'PLTE', b''.join(bytes(c[:3]) for c in palette)))
    if trns is not None:
        out.append(_chunk(b'tRNS', trns))
    for k, v in (text or {}).items():
        out.append(_chunk(b'tEXt', k.encode('latin1') + b'\0' + str(v).encode('latin1')))
    out.append(_chunk(b'IDAT', zlib.compress(raw, 9)))
    out.append(_chunk(b'IEND', b''))
    with open(path, 'wb') as f:
        f.write(b''.join(out))
