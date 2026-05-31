from PIL import Image
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC  = ROOT / "sprites" / "Nau.png"
OUT  = ROOT / "gfx_ship_anim.h"

img = Image.open(SRC).convert("RGBA")
W, H = 32, 24

# -----------------------------------------------------------------------
# Map source RGB colors -> Amiga palette index (1..31, 0=transparent/bg)
# Colors from the PNG, mapped to 12-bit Amiga format #RGB
# We assign indices carefully so related colors cluster in bitplanes.
# -----------------------------------------------------------------------

# Amiga palette slot -> 12-bit color #RGB
AMIGA_PALETTE = {
    # slot : (R4, G4, B4)   <- 4-bit per channel
    # Background (slot 0) = black, not used for ship
     1: (0x1, 0x1, 0x2),   # Very dark navy  (RGB 26,26,46)
     2: (0x2, 0x2, 0x4),   # Dark navy       (RGB 34,34,68)
     3: (0x3, 0x4, 0x6),   # Mid blue-grey   (RGB 58,74,106)
     4: (0x6, 0x8, 0xA),   # Light blue-grey (RGB 110,138,170)
     5: (0x4, 0x6, 0x8),   # Blue-grey       (RGB 78,106,138)
     6: (0x3, 0x1, 0x1),   # Dark rust       (RGB 58,32,32)
     7: (0x6, 0x3, 0x1),   # Mid orange-rust (RGB 112,64,32)
     8: (0xB, 0x5, 0x1),   # Orange          (RGB 192,96,32)
     9: (0xF, 0xA, 0x3),   # Bright yellow   (RGB 255,176,64)
    10: (0x0, 0x3, 0x1),   # Dark green      (RGB 16,56,32)
    11: (0x9, 0xA, 0xC),   # Light grey      (RGB 160,184,208)
    12: (0xD, 0xE, 0xF),   # Near white      (RGB 224,240,255)
}

# Source RGB -> palette index (nearest match)
def nearest_index(r, g, b):
    best_idx = 1
    best_dist = 999999
    r4, g4, b4 = r >> 4, g >> 4, b >> 4
    for idx, (pr, pg, pb) in AMIGA_PALETTE.items():
        dist = (r4-pr)**2 + (g4-pg)**2 + (b4-pb)**2
        if dist < best_dist:
            best_dist = dist
            best_idx = idx
    return best_idx

# Build per-pixel index map
pix_idx = []
for y in range(H):
    row = []
    for x in range(W):
        px = img.getpixel((x, y))
        if px[3] < 128:
            row.append(0)  # transparent
        else:
            row.append(nearest_index(px[0], px[1], px[2]))
    pix_idx.append(row)

# Build 5 bitplanes + mask
def build_words(row_vals):
    """row_vals: list of 32 colour indices (0=transparent)"""
    planes = [0, 0, 0, 0, 0]
    mask = 0
    for x, idx in enumerate(row_vals):
        if idx == 0:
            continue
        bit = 1 << (31 - x)
        mask |= bit
        for p in range(5):
            if idx & (1 << p):
                planes[p] |= bit
    # Split each 32-bit value into two 16-bit words (hi, lo)
    result = []
    result.append(((mask >> 16) & 0xFFFF, mask & 0xFFFF))
    for p in range(5):
        result.append(((planes[p] >> 16) & 0xFFFF, planes[p] & 0xFFFF))
    return result  # [mask_hi_lo, p0_hi_lo, p1_hi_lo, ...]

rows_data = [build_words(pix_idx[y]) for y in range(H)]
# rows_data[y][0] = (mask_hi, mask_lo)
# rows_data[y][1..5] = (plane_hi, plane_lo)

lines = []
lines.append("#pragma once")
lines.append("// Auto-generated from sprites/Nau.png - full colour 5-bitplane version")
lines.append(f"// Amiga palette: {len(AMIGA_PALETTE)} colours + transparent bg")
lines.append("#define SHIP_ANIM_WIDTH_WORDS 2")
lines.append("#define SHIP_ANIM_HEIGHT      24")
lines.append("#define SHIP_ANIM_NUM_FRAMES  4")
lines.append("")

# Copper palette setup macro
lines.append("// Call SetShipPalette() once after copper list init to load ship colours")
lines.append("// Adjust slot offset to match your game's palette layout (default: slots 1-12)")
lines.append("#define SHIP_PAL_OFFSET 1")
lines.append("static const UWORD SHIP_PALETTE_12BIT[] = {")
for idx in range(1, 13):
    r, g, b = AMIGA_PALETTE.get(idx, (0,0,0))
    lines.append(f"    0x{r:X}{g:X}{b:X},  // slot {idx}")
lines.append("};")
lines.append("")

# Mask array (shared across all 4 frames - hull shape doesn't change)
lines.append("static const UWORD SHIP_MASK[24*2] = {")
for y in range(H):
    mh, ml = rows_data[y][0]
    lines.append(f"    0x{mh:04X}, 0x{ml:04X},  // row {y}")
lines.append("};")
lines.append("")

# 5 bitplane arrays (shared across frames - static ship)
for p in range(5):
    lines.append(f"static const UWORD SHIP_PLANE{p}[24*2] = {{")
    for y in range(H):
        ph, pl = rows_data[y][p+1]
        lines.append(f"    0x{ph:04X}, 0x{pl:04X},  // row {y}")
    lines.append("};")
    lines.append("")

# For compatibility with existing DrawShipAnim call:
# All 4 frames point to same data (animation = engine glow only, added later)
for i in range(4):
    lines.append(f"#define SHIP_FRAME{i}_MASK SHIP_MASK")
    lines.append(f"#define SHIP_FRAME{i}_DATA SHIP_MASK")

lines.append("")
lines.append("static const UWORD* const SHIP_ANIM_MASKS[4] = {")
lines.append("    SHIP_FRAME0_MASK, SHIP_FRAME1_MASK, SHIP_FRAME2_MASK, SHIP_FRAME3_MASK")
lines.append("};")
lines.append("static const UWORD* const SHIP_ANIM_DATA[4] = {")
lines.append("    SHIP_FRAME0_DATA, SHIP_FRAME1_DATA, SHIP_FRAME2_DATA, SHIP_FRAME3_DATA")
lines.append("};")

# Bitplane arrays for the colored draw function
lines.append("")
lines.append("static const UWORD* const SHIP_PLANES[5] = {")
lines.append("    SHIP_PLANE0, SHIP_PLANE1, SHIP_PLANE2, SHIP_PLANE3, SHIP_PLANE4")
lines.append("};")

OUT.write_text("\n".join(lines) + "\n", encoding="ascii")
print(f"Wrote {OUT}")
print("Palette slots used: 1-12")
