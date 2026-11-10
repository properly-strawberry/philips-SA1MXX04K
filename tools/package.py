#!/usr/bin/env python3
"""Assemble the portable Windows package from the locally provided FFmpeg SDK."""
from pathlib import Path
import hashlib
import json
import shutil
import zipfile

root = Path(__file__).resolve().parents[1]
dist = root / 'dist'
sdk = root / 'third_party/ffmpeg'
dll_files = []
for prefix in ('avcodec-', 'avformat-', 'avutil-', 'swresample-'):
    candidates = list((sdk/'bin').glob(prefix+'*.dll'))
    if len(candidates) != 1:
        raise SystemExit(f'Expected one {prefix} DLL, got {candidates}')
    target = dist/candidates[0].name
    # Do not rewrite unchanged DLLs that may be loaded by a running player.
    if not target.exists() or hashlib.sha256(target.read_bytes()).digest() != hashlib.sha256(candidates[0].read_bytes()).digest():
        shutil.copy2(candidates[0], target)
    dll_files.append(target)
(dist/'Music').mkdir(exist_ok=True)
(dist/'Recordings'/'Voice').mkdir(parents=True, exist_ok=True)
(dist/'Playlists').mkdir(exist_ok=True)
templates = list((root/'assets'/'Playlists').iterdir())
for template in templates:
    target = dist/'Playlists'/template.name
    if not target.exists():
        shutil.copy2(template, target)
shutil.copy2(root/'PLAYER.md', dist/'README.txt')
shutil.copy2(root/'third_party/FFMPEG.md', dist/'FFMPEG.txt')
shutil.copy2(sdk/'LICENSE.txt', dist/'FFMPEG-LICENSE.txt')
files = [dist/name for name in ('philips.exe', 'README.txt', 'FFMPEG.txt', 'FFMPEG-LICENSE.txt')] + dll_files
manifest = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
(dist/'SHA256SUMS.json').write_text(json.dumps(manifest, indent=2)+'\n')
archive = dist/'philips-win64.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for p in files+[dist/'SHA256SUMS.json']:
        z.write(p, 'philips/'+p.name)
    z.writestr('philips/Music/', '')
    z.writestr('philips/Recordings/Voice/', '')
    for template in templates:
        # Distribute fresh templates; never package personal station/playlist data.
        z.write(template, 'philips/Playlists/'+template.name)
print(f'Portable package: {archive}')
