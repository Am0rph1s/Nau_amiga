#!/usr/bin/env python3
"""Extract current shot sprite arrays from gfx.h as 1:1 RGBA PNGs.

White polarity: accent BPL2(reg 9=white), body BPL6(reg 12=light blue)
Black polarity: accent BPL4(reg 10=black), body BPL2+BPL4(reg 11=red)
"""
import re, os
from PIL import Image

GFX_H   = os.path.join(os.path.dirname(__file__), "..", "gfx.h")
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "shots")
os.makedirs(OUT_DIR, exist_ok=True)

def parse_uwords(text):
    nums = re.findall(r'0x[0-9A-Fa-f]+', text)
    return [int(n, 16) for n in nums]

with open(GFX_H) as f:
    src = f.read()

def get_decl(name):
    m = re.search(r'static const UWORD\s+' + name + r'\[(\d+)\]\s*=\s*\{([^}]+)\}', src)
    if not m:
        raise ValueError(f"Array {name} not found")
    return parse_uwords(m.group(2))

arrays = {}
for name in ['g_ShotW_Mask', 'g_ShotW_DataHi', 'g_ShotW_DataLo',
             'g_ShotB_Mask', 'g_ShotB_DataHi', 'g_ShotB_DataLo',
             'g_EShotW_Mask', 'g_EShotW_DataHi', 'g_EShotW_DataLo',
             'g_EShotB_Mask', 'g_EShotB_DataHi', 'g_EShotB_DataLo']:
    arrays[name] = get_decl(name)

COL_PAL = {
    'white': {
        'accent': (255, 255, 255, 255),  # BPL2 (color 9=white)
        'body':   (0, 204, 255, 255),    # BPL6 (color 12=light blue)
    },
    'black': {
        'accent': (0, 0, 0, 255),        # BPL4 (color 10=dark)
        'body':   (255, 0, 0, 255),      # BPL4+BPL6 (color 14=red)
    },
}
TRANS = (0, 0, 0, 0)

def render(mask, data_hi, data_lo, pal, path):
    H = len(mask)
    img = Image.new('RGBA', (16, H), TRANS)
    pix = img.load()
    for y in range(H):
        for x in range(16):
            bit = 0x8000 >> x
            if not (mask[y] & bit):
                continue
            if data_hi[y] & bit:
                col = pal['accent']
            elif data_lo[y] & bit:
                col = pal['body']
            else:
                col = (255, 0, 255, 255)
            pix[x, y] = col
    img.save(path)
    print(f"  -> {os.path.basename(path)}  ({img.width}x{img.height})")

render(arrays['g_ShotW_Mask'], arrays['g_ShotW_DataHi'], arrays['g_ShotW_DataLo'],
       COL_PAL['white'], os.path.join(OUT_DIR, 'shot_player_white.png'))
render(arrays['g_ShotB_Mask'], arrays['g_ShotB_DataHi'], arrays['g_ShotB_DataLo'],
       COL_PAL['black'], os.path.join(OUT_DIR, 'shot_player_black.png'))
render(arrays['g_EShotW_Mask'], arrays['g_EShotW_DataHi'], arrays['g_EShotW_DataLo'],
       COL_PAL['white'], os.path.join(OUT_DIR, 'shot_enemy_white.png'))
render(arrays['g_EShotB_Mask'], arrays['g_EShotB_DataHi'], arrays['g_EShotB_DataLo'],
       COL_PAL['black'], os.path.join(OUT_DIR, 'shot_enemy_black.png'))

print()
print("DONE")
