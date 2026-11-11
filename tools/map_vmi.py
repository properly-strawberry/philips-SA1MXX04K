#!/usr/bin/env python3
"""Build analysis-only VMI ELF images from SB LOAD anchors; no device writes.

Uses the shared SB extractor's ELF writer. The 0xea000 candidate is supported
by two byte-identical resident LOADs in each VMI; SB bytes win on overlays.
The Thumb seed at 0x100000 is an analysis seed, not the SB boot entry point.
"""
import hashlib
import json
from pathlib import Path
import runpy

ROOT = Path(__file__).resolve().parents[1]
elf32 = runpy.run_path(str(ROOT / 'tools/sb_extract.py'))['elf32']
manifest = json.loads((ROOT / 'gen/sb/manifest.json').read_text())
output = ROOT / 'gen/deep'
output.mkdir(parents=True, exist_ok=True)
for mode, section_index, vmi_index in [('host', 1, 3), ('play', 2, 4)]:
    section = manifest['sections'][section_index]
    vmi_section = manifest['sections'][vmi_index]
    stem = f'{section_index:02}_{mode}'
    raw = (ROOT / f'gen/sb/{stem}.bin').read_bytes()
    virtual = bytearray((ROOT / f'gen/sb/{vmi_index:02}_{vmi_section["name"]}.bin').read_bytes())
    assert hashlib.sha256(raw).hexdigest() == section['sha256']
    assert hashlib.sha256(virtual).hexdigest() == vmi_section['sha256']
    # Retain only the final boot-command stage; earlier stages reuse addresses.
    pending, final, entry = [], None, None
    for command in section['commands']:
        opcode = command['opcode']
        address, size = int(command['address'], 16), command['length']
        if opcode == 2:
            data = raw[command['offset'] + 16:command['offset'] + 16 + size]
            assert len(data) == size
            pending.append((address, data, size))
        elif opcode == 3:
            width = 1 << command['flags']
            pattern = int(command['value'], 16).to_bytes(4, 'little')[:width]
            pending.append((address, (pattern * ((size + width - 1)//width))[:size], size))
        elif opcode in (4, 5):
            final, pending, entry = pending, [], address
    assert final and entry is not None and not pending
    base, anchors, low = 0xea000, [], []
    for address, data, size in final:
        if address < base:
            assert address + size <= base
            low.append((address, data, size))
            continue
        offset = address - base
        assert 0 <= offset < offset + size <= len(virtual)
        anchors.append({'address': hex(address), 'length': size,
                        'matches_vmi': virtual[offset:offset + size] == data})
        virtual[offset:offset + size] = data
    assert sum(a['length'] > 1024 and a['matches_vmi'] for a in anchors) >= 2
    name = vmi_section['name']
    result = bytes(elf32(low + [(base, bytes(virtual), len(virtual))], 0x100001))
    path = output / f'{name}-mapped.elf'
    path.write_bytes(result)
    record = {'mode': mode, 'vmi_base': hex(base), 'analysis_thumb_seed': '0x100000',
              'actual_sb_entry': hex(entry), 'known_sb_anchors': anchors,
              'sb_overlay_wins': True, 'elf_sha256': hashlib.sha256(result).hexdigest(),
              'mapping_status': 'derived; resident anchors validate the candidate'}
    (output / f'{name}-mapping.json').write_text(json.dumps(record, indent=2) + '\n')
    print(f'{name}: {record["elf_sha256"]}, {len(anchors)} anchors -> {path}')
