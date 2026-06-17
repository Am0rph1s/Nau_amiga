#!/usr/bin/env python3
"""Import edited shot PNGs, update gfx.h with split-polarity arrays.

Colour → plane mapping:
  white: (255,255,255)=accent→BPL2(reg 9=white), (0,204,255)=body→BPL6(reg 12=per-pol)
  black: (0,0,0)|(12,12,12)=accent→BPL4(reg 10=black), (255,0,0)=body→BPL2→OR BPL4→reg 11(per-pol)
"""
from PIL import Image
import os, sys

SHOTS_DIR = os.path.join(os.path.dirname(__file__), "..", "shots")
GFX_H     = os.path.join(os.path.dirname(__file__), "..", "gfx.h")

EXACT = {
    (255, 255, 255): 'accent',
    (0, 204, 255):   'body',
    (255, 0, 0):     'body',
    (0, 0, 0):       'accent',
    (12, 12, 12):    'accent',
}

def png_to_arrays(png_path):
    img = Image.open(png_path).convert('RGBA')
    H = img.height
    mask   = [0] * H
    accent = [0] * H
    body   = [0] * H
    for y in range(H):
        for x in range(16):
            r, g, b, a = img.getpixel((x, y))
            if a < 128:
                continue
            kind = EXACT.get((r, g, b), 'body')
            bit = 0x8000 >> x
            mask[y] |= bit
            if kind == 'accent':
                accent[y] |= bit
            else:
                body[y] |= bit
    for y in range(H):
        body[y] &= ~accent[y]
    return mask, body, accent

def fmt(vals):
    return '{ ' + ','.join(hex(v) for v in vals) + ' }'

files = [
    ('g_ShotW_Mask',   'g_ShotW_DataHi', 'g_ShotW_DataLo',
     'shot_player_white.png'),
    ('g_ShotB_Mask',   'g_ShotB_DataHi', 'g_ShotB_DataLo',
     'shot_player_black.png'),
    ('g_EShotW_Mask',  'g_EShotW_DataHi','g_EShotW_DataLo',
     'shot_enemy_white.png'),
    ('g_EShotB_Mask',  'g_EShotB_DataHi','g_EShotB_DataLo',
     'shot_enemy_black.png'),
]

blocks = {}
for nm_m, nm_h, nm_l, png in files:
    path = os.path.join(SHOTS_DIR, png)
    mask, body, accent = png_to_arrays(path)
    blocks[nm_m] = mask
    blocks[nm_h] = accent
    blocks[nm_l] = body

with open(GFX_H) as f:
    text = f.read()

# Replace from "// --- PLAYER SHOT" up to (but not including) "// --- EXPLOSION"
old_start = '// --- PLAYER SHOT'
old_end   = '// --- EXPLOSION'

idx_start = text.find(old_start)
idx_end   = text.find(old_end, idx_start)
if idx_start < 0 or idx_end < 0:
    print("ERROR: section markers not found")
    sys.exit(1)

# Find next newline after idx_start
line_start = text.rfind('\n', 0, idx_start) + 1 if text.rfind('\n', 0, idx_start) >= 0 else 0

new_section = """\
// --- PLAYER SHOT (4x8, 2-tone, split per polarity) -------------------------
// White polarity: accent BPL2(reg 9=white) + body BPL6(reg 12=light blue)
// Black polarity: accent BPL4(reg 10=black) + body BPL2+BPL4(reg 11=red)
// body and accent NEVER overlap in the same pixel.
static const UWORD g_ShotW_Mask[8]   = """ + fmt(blocks['g_ShotW_Mask']) + """;
static const UWORD g_ShotW_DataHi[8] = """ + fmt(blocks['g_ShotW_DataHi']) + """;
static const UWORD g_ShotW_DataLo[8] = """ + fmt(blocks['g_ShotW_DataLo']) + """;

static const UWORD g_ShotB_Mask[8]   = """ + fmt(blocks['g_ShotB_Mask']) + """;
static const UWORD g_ShotB_DataHi[8] = """ + fmt(blocks['g_ShotB_DataHi']) + """;
static const UWORD g_ShotB_DataLo[8] = """ + fmt(blocks['g_ShotB_DataLo']) + """;

// --- ENEMY SHOT (10x4 diamond, 2-tone, split per polarity) ------------------
static const UWORD g_EShotW_Mask[8]   = """ + fmt(blocks['g_EShotW_Mask']) + """;
static const UWORD g_EShotW_DataHi[8] = """ + fmt(blocks['g_EShotW_DataHi']) + """;
static const UWORD g_EShotW_DataLo[8] = """ + fmt(blocks['g_EShotW_DataLo']) + """;

static const UWORD g_EShotB_Mask[8]   = """ + fmt(blocks['g_EShotB_Mask']) + """;
static const UWORD g_EShotB_DataHi[8] = """ + fmt(blocks['g_EShotB_DataHi']) + """;
static const UWORD g_EShotB_DataLo[8] = """ + fmt(blocks['g_EShotB_DataLo']) + """;

"""

text = text[:line_start] + new_section + text[idx_end:]

with open(GFX_H, 'w') as f:
    f.write(text)

print("Regenerated shots:")
for key in sorted(blocks):
    print(f"  {key:20s} = {fmt(blocks[key])}")
print()
print("gfx.h updated.")
