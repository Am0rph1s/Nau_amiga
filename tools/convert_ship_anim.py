from PIL import Image
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "sprites" / "nau_4_frames.png"
OUT = ROOT / "gfx_ship_anim.h"

FRAME_W = 32
FRAME_H = 24
N_FRAMES = 4

# Crop boxes tuned for the provided sheet: 4 ship frames on a grid background.
# The source image is a screenshot-like sheet; we detect non-background pixels by alpha
# if present, otherwise by color difference from the top-left grid background.

def is_sprite_pixel(px, bg):
    if len(px) == 4 and px[3] < 16:
        return False
    r, g, b = px[:3]
    mx = max(r, g, b)
    mn = min(r, g, b)
    # Ship pixels are metallic/blue/orange and stand out from the dark grid.
    # Keep bright or saturated pixels, reject the grey grid/checker background.
    return mx > 85 and (mx - mn > 18 or mx > 145)

img = Image.open(SRC).convert("RGBA")
w, h = img.size
bg = img.getpixel((0, 0))

frames = []
# Manual boxes for the 1408x768 source. Each box contains a full ship frame.
boxes_1408 = [
    (120, 235, 220, 350),
    (370, 235, 470, 420),
    (620, 235, 720, 420),
    (870, 235, 970, 430),
]
boxes = []
for x0, y0, x1, y1 in boxes_1408:
    box = (
        int(x0 * w / 1408.0),
        int(y0 * h / 768.0),
        int(x1 * w / 1408.0),
        int(y1 * h / 768.0),
    )
    boxes.append(box)
    crop = img.crop(box)
    crop = crop.resize((FRAME_W, FRAME_H), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (FRAME_W, FRAME_H), (0, 0, 0, 0))
    canvas.alpha_composite(crop, (0, 0))
    frames.append(canvas)

# Palette-to-1bpp: create mask/data as opaque vs transparent/background.
# For now draw solid color using mask=data. This guarantees shape fidelity first.

def row_words(frame, y):
    words = []
    for word_i in range(2):
        val = 0
        for bit in range(16):
            x = word_i * 16 + bit
            px = frame.getpixel((x, y))
            if is_sprite_pixel(px, bg):
                val |= 1 << (15 - bit)
        words.append(val)
    return words

lines = []
lines.append("#pragma once")
lines.append("// Auto-generated from sprites/nau_4_frames.png")
lines.append("#define SHIP_ANIM_WIDTH_WORDS 2")
lines.append("#define SHIP_ANIM_HEIGHT 24")
lines.append("#define SHIP_ANIM_NUM_FRAMES 4")
lines.append("")

for i, frame in enumerate(frames):
    lines.append(f"static const UWORD SHIP_FRAME{i}_MASK[24*2] = {{")
    for y in range(FRAME_H):
        w0, w1 = row_words(frame, y)
        lines.append(f"    0x{w0:04X}, 0x{w1:04X},")
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
print(f"Wrote {OUT}")
print("Boxes:", boxes)
