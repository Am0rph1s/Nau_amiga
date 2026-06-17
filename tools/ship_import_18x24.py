"""Importa Nova_24x18_A/B.png (18x24) i regenera gfx_ship_white.h."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC_A = ROOT / "sprites" / "Nova_24x18_A.png"
SRC_B = ROOT / "sprites" / "Nova_24x18_b.png"
OUT   = ROOT / "gfx_ship_white.h"

FRAME_W = 18
FRAME_H = 24
N_WORDS = 2

try:
    from PIL import Image
except ImportError:
    raise SystemExit("Cal Pillow: pip install Pillow")

def color_to_planes(px):
    if len(px) == 4 and px[3] < 16:
        return (0, 0, 0)
    r, g, b = px[0], px[1], px[2]
    if r > 200 and g > 200 and b > 200:
        return (1, 0, 0)
    if r < 60 and g < 60 and b < 60:
        return (0, 0, 1)
    return (0, 1, 0)

def png_to_planes(path):
    img = Image.open(path).convert("RGBA")
    if img.size != (FRAME_W, FRAME_H):
        raise SystemExit(f"{path}: mida {img.size}, esperada ({FRAME_W}, {FRAME_H})")
    mask_words   = [0] * (FRAME_H * N_WORDS)
    plane0_words = [0] * (FRAME_H * N_WORDS)
    plane1_words = [0] * (FRAME_H * N_WORDS)
    plane2_words = [0] * (FRAME_H * N_WORDS)
    plane3_words = [0] * (FRAME_H * N_WORDS)
    for y in range(FRAME_H):
        for word_i in range(N_WORDS):
            word_idx = y * N_WORDS + word_i
            for bit in range(16):
                x = word_i * 16 + bit
                if x >= FRAME_W:
                    break
                p0, p1, p2 = color_to_planes(img.getpixel((x, y)))
                bit_mask = 1 << (15 - bit)
                if p0 or p1 or p2:
                    mask_words[word_idx] |= bit_mask
                if p0:
                    plane0_words[word_idx] |= bit_mask
                if p1:
                    plane1_words[word_idx] |= bit_mask
                if p2:
                    plane2_words[word_idx] |= bit_mask
    return mask_words, plane0_words, plane1_words, plane2_words, plane3_words

def format_words(words, indent="    "):
    lines = []
    for i in range(0, len(words), 4):
        chunk = words[i:i+4]
        line = indent + ", ".join(f"0x{w:04X}" for w in chunk) + ","
        lines.append(line)
    return "\n".join(lines)

print(f"Llegint {SRC_A.name}...")
a = png_to_planes(SRC_A)
print(f"Llegint {SRC_B.name}...")
b = png_to_planes(SRC_B)

content = f"""#pragma once
// Auto-generated from {SRC_A.name} (colorMode=0)
// and {SRC_B.name} (colorMode=1).
#define SHIP_W_WIDTH   {FRAME_W}
#define SHIP_W_HEIGHT  {FRAME_H}
#define SHIP_W_BPL     4

// === POLARITY A (colorMode=0) ===
static const UWORD SHIP_W_A_MASK[{FRAME_H*N_WORDS}] = {{
{format_words(a[0])}
}};
static const UWORD SHIP_W_A_PLANE0[{FRAME_H*N_WORDS}] = {{
{format_words(a[1])}
}};
static const UWORD SHIP_W_A_PLANE1[{FRAME_H*N_WORDS}] = {{
{format_words(a[2])}
}};
static const UWORD SHIP_W_A_PLANE2[{FRAME_H*N_WORDS}] = {{
{format_words(a[3])}
}};
static const UWORD SHIP_W_A_PLANE3[{FRAME_H*N_WORDS}] = {{
{format_words(a[4])}
}};
static const UWORD* const SHIP_W_A_PLANES[4] = {{
    SHIP_W_A_PLANE0, SHIP_W_A_PLANE1, SHIP_W_A_PLANE2, SHIP_W_A_PLANE3
}};

// === POLARITY B (colorMode=1) ===
static const UWORD SHIP_W_B_MASK[{FRAME_H*N_WORDS}] = {{
{format_words(b[0])}
}};
static const UWORD SHIP_W_B_PLANE0[{FRAME_H*N_WORDS}] = {{
{format_words(b[1])}
}};
static const UWORD SHIP_W_B_PLANE1[{FRAME_H*N_WORDS}] = {{
{format_words(b[2])}
}};
static const UWORD SHIP_W_B_PLANE2[{FRAME_H*N_WORDS}] = {{
{format_words(b[3])}
}};
static const UWORD SHIP_W_B_PLANE3[{FRAME_H*N_WORDS}] = {{
{format_words(b[4])}
}};
static const UWORD* const SHIP_W_B_PLANES[4] = {{
    SHIP_W_B_PLANE0, SHIP_W_B_PLANE1, SHIP_W_B_PLANE2, SHIP_W_B_PLANE3
}};
"""

OUT.write_text(content, encoding="ascii")
print(f"Escrit {OUT}")
