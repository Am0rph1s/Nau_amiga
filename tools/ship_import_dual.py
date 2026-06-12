"""Importa els dos PNGs de la nau (polaritat A i polaritat B) i regenera
gfx_ship_white.h ambdues versions de 4 bitplanes, preparat per DrawShipAnim.

Estructura generada:
  SHIP_W_A_PLANE0..3[48] + SHIP_W_A_MASK[48] + SHIP_W_A_PLANES[4]  (per colorMode=0)
  SHIP_W_B_PLANE0..3[48] + SHIP_W_B_MASK[48] + SHIP_W_B_PLANES[4]  (per colorMode=1)

Mapeig de colors a (lo, hi) - el mateix per ambdues polaritats:
  transparent       -> (0, 0)
  BLANC  (R>0xB0)   -> (1, 0)  <- cos (en el dibuix que hauries fet en A o B)
  FOSC   (R<0x40)   -> (0, 1)  <- contorn
  GRIS CLAR         -> (1, 1)  <- interior / highlight
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC_A = ROOT / "sprites" / "nau_fire_polarity_a.png"
SRC_B = ROOT / "sprites" / "nau_fire_polarity_B.png"
OUT  = ROOT / "gfx_ship_white.h"

FRAME_W = 32
FRAME_H = 24
N_WORDS = FRAME_W // 16  # 2

try:
    from PIL import Image
except ImportError:
    raise SystemExit("Cal Pillow: pip install Pillow")

def color_to_lh(px):
    if len(px) == 4 and px[3] < 16:
        return (0, 0)
    r = px[0]
    if r < 0x40:
        return (0, 1)  # fosc -> hi (contorn)
    elif r > 0xB0:
        return (1, 0)  # blanc -> lo (cos)
    else:
        return (1, 1)  # gris clar -> lo+hi (interior)

def png_to_planes(path):
    img = Image.open(path).convert("RGBA")
    if img.size != (FRAME_W, FRAME_H):
        raise SystemExit(f"{path}: mida {img.size}, esperada ({FRAME_W}, {FRAME_H})")

    mask_words  = [0] * (FRAME_H * N_WORDS)
    plane0_words = [0] * (FRAME_H * N_WORDS)  # bit 0 de lo
    plane1_words = [0] * (FRAME_H * N_WORDS)  # bit 1 de lo (no usat)
    plane2_words = [0] * (FRAME_H * N_WORDS)  # bit 0 de hi
    plane3_words = [0] * (FRAME_H * N_WORDS)  # bit 1 de hi (no usat)

    for y in range(FRAME_H):
        for word_i in range(N_WORDS):
            word_idx = y * N_WORDS + word_i
            for bit in range(16):
                x = word_i * 16 + bit
                lo, hi = color_to_lh(img.getpixel((x, y)))
                bit_mask = 1 << (15 - bit)
                if lo or hi:
                    mask_words[word_idx] |= bit_mask
                if lo:
                    plane0_words[word_idx] |= bit_mask
                if hi:
                    plane2_words[word_idx] |= bit_mask
                # plane1 i plane3 = 0
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
// Auto-generated from sprites/nau_fire_polarity_a.png (colorMode=0)
// and sprites/nau_fire_polarity_B.png (colorMode=1).
//   plane0 + plane1 -> "lo" (PF2 bit 0)
//   plane2 + plane3 -> "hi" (PF2 bit 1)
#define SHIP_W_WIDTH   {FRAME_W}
#define SHIP_W_HEIGHT  {FRAME_H}
#define SHIP_W_BPL     4

// Amiga greyscale palette (14 entries, slot 0 = background black):
static const UWORD SHIP_W_PALETTE[14] = {{
    0x0000,  // [ 0] RGB(0, 0, 0)
    0x0111,  // [ 1] RGB(24, 24, 24)
    0x0222,  // [ 2] RGB(40, 40, 40)
    0x0333,  // [ 3] RGB(56, 56, 56)
    0x0444,  // [ 4] RGB(71, 71, 71)
    0x0555,  // [ 5] RGB(86, 86, 86)
    0x0666,  // [ 6] RGB(100, 100, 100)
    0x0777,  // [ 7] RGB(113, 113, 113)
    0x0777,  // [ 8] RGB(126, 126, 126)
    0x0888,  // [ 9] RGB(140, 140, 140)
    0x0999,  // [10] RGB(155, 155, 155)
    0x0AAA,  // [11] RGB(171, 171, 171)
    0x0BBB,  // [12] RGB(189, 189, 189)
    0x0DDD  // [13] RGB(209, 209, 209)
}};

// === POLARITY A (colorMode=0, estat normal sense foc) ===
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

// === POLARITY B (colorMode=1, fire apretat) ===
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

// Backward compat: keep old names pointing to polarity A data.
#define SHIP_W_MASK SHIP_W_A_MASK
#define SHIP_W_PLANE0 SHIP_W_A_PLANE0
#define SHIP_W_PLANE1 SHIP_W_A_PLANE1
#define SHIP_W_PLANE2 SHIP_W_A_PLANE2
#define SHIP_W_PLANE3 SHIP_W_A_PLANE3
#define SHIP_W_PLANES SHIP_W_A_PLANES
"""

OUT.write_text(content, encoding="ascii")
print(f"Escrit {OUT}")
print()
print("IMPORTANT: tambe cal modificar DrawShipAnim perque trii entre")
print("SHIP_W_A_PLANES i SHIP_W_B_PLANES segons colorMode (en lloc del swap).")
