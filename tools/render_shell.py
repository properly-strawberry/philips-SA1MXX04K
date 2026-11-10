#!/usr/bin/env python3
"""Prerender assets/shell/shell.svg into the PNGs the Windows program embeds.

Draws with resvg (the resvg-py wheel in the workspace virtual environment):

    ../.venv/bin/python -I tools/render_shell.py

Output goes to gen/player/shell/ at twice the window's logical size. The rest
picture is whole; each pressed state is a patch with a margin; the handle is
cut out alone on a transparent background. The patch rectangles (logical
pixels) are repeated in src/main.cpp.
"""
import re
import sys
from pathlib import Path

import resvg_py

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets/shell/shell.svg'
OUT = ROOT / 'gen/player/shell'
SCALE = 2
WIDTH, HEIGHT = 920, 412
ROCKER, PAD, HANDLE = (175, 17, 266, 44), (612, 68, 274, 280), (655, 23, 110, 34)
STATES = ('rocker-rest', 'rocker-minus', 'rocker-plus', 'pad-up', 'pad-down', 'pad-back', 'pad-play', 'handle')
SIZE = f'width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}"'
BACKGROUND = f'<rect width="{WIDTH}" height="{HEIGHT}" fill="#181b21"/>'
# The lettering is set in URW Nimbus Sans Bold; name the file so the result does not depend on font lookup.
FONT = Path('/usr/share/fonts/opentype/urw-base35/NimbusSans-Bold.otf')


def render(svg, name, shown, crop=None, scene=True):
    """Draw the picture with the given state groups shown, optionally only a rectangle of it."""
    for state in STATES:
        marker = f'id="state-{state}"'
        svg = re.sub(re.escape(marker) + r'( display="none")?', marker + ('' if state in shown else ' display="none"'), svg)
    if not scene:
        svg = svg.replace('<g id="scene">', '<g id="scene" display="none">').replace(BACKGROUND, '')
    if crop:
        x, y, w, h = crop
        svg = svg.replace(SIZE, f'width="{w}" height="{h}" viewBox="{x} {y} {w} {h}"')
    (OUT / f'{name}.png').write_bytes(bytes(resvg_py.svg_to_bytes(svg_string=svg, zoom=SCALE, font_files=[str(FONT)])))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    svg = SOURCE.read_text()
    if not FONT.exists() or svg.count(SIZE) != 1 or svg.count(BACKGROUND) != 1 or svg.count('<g id="scene">') != 1:
        sys.exit(f'{FONT} is missing, or {SOURCE} lost its size, background or scene group')
    for state in STATES:
        if svg.count(f'id="state-{state}"') != 1:
            sys.exit(f'{SOURCE} needs exactly one group with id state-{state}')
    render(svg, 'base', {'rocker-rest'})
    render(svg, 'rocker_minus', {'rocker-minus'}, ROCKER)
    render(svg, 'rocker_plus', {'rocker-plus'}, ROCKER)
    for lean in ('up', 'down', 'back', 'play'):
        render(svg, f'pad_{lean}', {'rocker-rest', f'pad-{lean}'}, PAD)
    render(svg, 'handle', {'handle'}, HANDLE, scene=False)
    # A picture of everything pressed, for looking over; the program does not use it.
    render(svg, 'preview', {'rocker-plus', 'pad-up', 'handle'})


if __name__ == '__main__':
    main()
