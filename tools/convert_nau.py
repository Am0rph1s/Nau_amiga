from PIL import Image
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC  = ROOT / "sprites" / "Nau.png"
OUT  = ROOT / "gfx_ship_anim.h"

img = Image.open(SRC).convert("RGBA")
w, h = img.size  # expected 32x24
assert w == 32 and h == 24, f"Expected 32x24, got {w}x{h}"

pixels = [img.getpixel((x, y)) for y in range(h) for x in range(w)]

# Transparent pixels = alpha < 128
def is_opaque(px): return px[3] >= 128

# Build mask words and collect palette colours (ignore transparent + black background)
mask_rows  = []
color_rows = []  # raw RGBA rows for palette analysis

for y in range(h):
    m0 = m1 = 0
    for x in range(w):
        px = img.getpixel((x, y))
        if is_opaque(px):
            if x < 16:
                m0 |= 1 << (15 - x)
            else:
                m1 |= 1 << (31 - x)
    mask_rows.append((m0, m1))

# Dump palette info to stdout so we can see what we're working with
palette = {}
for y in range(h):
    for x in range(w):
        px = img.getpixel((x, y))
        if is_opaque(px):
            key = (px[0]>>4, px[1]>>4, px[2]>>4)  # 4-bit per channel
            palette[key] = palette.get(key, 0) + 1

print("Palette (4-bit RGB, pixel count):")
for k, v in sorted(palette.items(), key=lambda x: -x[1]):
    print(f"  #{k[0]:X}{k[1]:X}{k[2]:X}  ({v} px)")

# Generate 4 mask frames:
#   Frame 0 = idle (same mask, minimal data)
#   Frames 1-3 = same hull mask, identical (animation will be added later per-frame)
# For now all 4 frames are identical hull shape so it compiles and displays correctly.

lines = []
lines.append("#pragma once")
lines.append("// Auto-generated from sprites/Nau.png (32x24, palette mode)")
lines.append("#define SHIP_ANIM_WIDTH_WORDS 2")
lines.append("#define SHIP_ANIM_HEIGHT      24")
lines.append("#define SHIP_ANIM_NUM_FRAMES  4")
lines.append("")

# Write the 4 frames (identical mask/data for now)
for i in range(4):
    lines.append(f"static const UWORD SHIP_FRAME{i}_MASK[24*2] = {{")
    for y in range(h):
        m0, m1 = mask_rows[y]
        lines.append(f"    0x{m0:04X}, 0x{m1:04X},  // row {y}")
    lines.append("};")
    lines.append(f"#define SHIP_FRAME{i}_DATA SHIP_FRAME{i}_MASK")
    lines.append("")

lines.append("static const UWORD* const SHIP_ANIM_MASKS[4] = {")
lines.append("    SHIP_FRAME0_MASK, SHIP_FRAME1_MASK, SHIP_FRAME2_MASK, SHIP_FRAME3_MASK")
lines.append("};")
lines.append("static const UWORD* const SHIP_ANIM_DATA[4] = {")
lines.append("    SHIP_FRAME0_DATA, SHIP_FRAME1_DATA, SHIP_FRAME2_DATA, SHIP_FRAME3_DATA")
lines.append("};")

OUT.write_text("\n".join(lines) + "\n", encoding="ascii")
print(f"\nWrote {OUT}  ({w}x{h})")
