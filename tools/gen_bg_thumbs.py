#!/usr/bin/env python3
"""Builds app/res/textures/backgrounds/vlither_thumbs.png, the preview sheet the
Settings > General > Background picker draws from.

One square cell per entry of BACKGROUNDS in app/src/game/backgrounds.h, in table
order, COLS to a row. Re-run this after appending a background:

    python3 tools/gen_bg_thumbs.py
"""
import os
import re
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "app/src/game/backgrounds.h")
OUT = os.path.join(ROOT, "app/res/textures/backgrounds/vlither_thumbs.png")

src = open(HEADER).read()
cols = int(re.search(r"BACKGROUND_THUMB_COLS\s+(\d+)", src).group(1))
cell = int(re.search(r"BACKGROUND_THUMB_CELL\s+(\d+)", src).group(1))
entries = re.findall(r'\{"(\w+)",\s*"([^"]+)",\s*(NULL|"[^"]+"),\s*[\d.]+f,\s*[\d.]+f\}', src)
rows = (len(entries) + cols - 1) // cols
sheet = Image.new("RGB", (cols * cell, rows * cell), (0, 0, 0))

for i, (ident, label, path) in enumerate(entries):
    if path == "NULL":
        # None shows the arena's own dark clear colour; Black is true black.
        colour = (0, 0, 0) if ident == "black" else (30, 34, 46)
        tile = Image.new("RGB", (cell, cell), colour)
    else:
        img = Image.open(os.path.join(ROOT, path.strip('"'))).convert("RGB")
        w, h = img.size
        side = min(w, h)  # centre square, so wide tiles are cropped not squashed
        left, top = (w - side) // 2, (h - side) // 2
        tile = img.crop((left, top, left + side, top + side)).resize(
            (cell, cell), Image.LANCZOS)
    sheet.paste(tile, ((i % cols) * cell, (i // cols) * cell))

sheet.save(OUT, optimize=True)
print(f"{len(entries)} cells -> {OUT} ({sheet.size[0]}x{sheet.size[1]})")
