"""Importa un PNG modificat de la nau (estat foc apretat) i regenera
gfx_ship_white.h amb l'estructura de 4 bitplanes esperada per DrawShipAnim.

Estructura esperada:
  SHIP_W_MASK[48]   = 1 on hi ha pixel, 0 on es transparent
  SHIP_W_PLANE0[48] = bit 0 de "lo" (cos en polaritat negra)
  SHIP_W_PLANE1[48] = bit 1 de "lo" (pot ser tot 0)
  SHIP_W_PLANE2[48] = bit 0 de "hi" (contorn en polaritat negra)
  SHIP_W_PLANE3[48] = bit 1 de "hi" (pot ser tot 0)

El DrawShipAnim fa: lo = plane0 | plane1, hi = plane2 | plane3.

El PNG ha de tenir aquesta paleta:
  transparent       -> pixel de fons
  FOSC  (R<0x40)    -> lo=1, hi=0  (cos en polaritat negra)
  BLANC (R>0xB0)    -> lo=0, hi=1  (contorn en polaritat negra)
  GRIS CLAR         -> lo=1, hi=1  (interior / highlight en polaritat negra)
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "sprites" / "nau_fire_held.png"
OUT = ROOT / "gfx_ship_white.h"

FRAME_W = 32
FRAME_H = 24
N_WORDS = FRAME_W // 16  # 2

try:
    from PIL import Image
except ImportError:
    raise SystemExit("Cal Pillow: pip install Pillow")

img = Image.open(SRC).convert("RGBA")
if img.size != (FRAME_W, FRAME_H):
    raise SystemExit(f"Mida del PNG incorrecta: {img.size}, esperada ({FRAME_W}, {FRAME_H})")

# Decideix (lo, hi) per pixel segons el color
def color_to_lh(px):
    if len(px) == 4 and px[3] < 16:
        return (0, 0)
    r = px[0]
    if r < 0x40:
        return (1, 0)  # fosc -> lo
    elif r > 0xB0:
        return (0, 1)  # blanc -> hi
    else:
        return (1, 1)  # gris clar -> lo+hi

# Construeix les 4 planes + mask
# Distribucio: ho posem tot al bit 0 de cada "meitat" (plane0 i plane2)
# Els altres 2 bitplanes (plane1 i plane3) queden a 0.
mask_words  = [0] * (FRAME_H * N_WORDS)
plane0_words = [0] * (FRAME_H * N_WORDS)  # lo
plane1_words = [0] * (FRAME_H * N_WORDS)
plane2_words = [0] * (FRAME_H * N_WORDS)  # hi
plane3_words = [0] * (FRAME_H * N_WORDS)

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
            # plane1 i plane3 queden a 0 per defecte

# Escriu el fitxer
def format_words(words, indent="    "):
    lines = []
    for i in range(0, len(words), 4):
        chunk = words[i:i+4]
        line = indent + ", ".join(f"0x{w:04X}" for w in chunk) + ","
        lines.append(line)
    return "\n".join(lines)

mask_str   = format_words(mask_words)
plane0_str = format_words(plane0_words)
plane1_str = format_words(plane1_words)
plane2_str = format_words(plane2_words)
plane3_str = format_words(plane3_words)

content = f"""#pragma once
// Auto-generated from sprites/nau_fire_held.png
// 4-bitplane greyscale version of the ship (black polarity / fire held state).
//   plane0 + plane1 -> "lo" (PF2 bit 0, cos en polaritat negra)
//   plane2 + plane3 -> "hi" (PF2 bit 1, contorn en polaritat negra)
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

static const UWORD SHIP_W_MASK[{FRAME_H*N_WORDS}] = {{
{mask_str}
}};

static const UWORD SHIP_W_PLANE0[{FRAME_H*N_WORDS}] = {{
{plane0_str}
}};

static const UWORD SHIP_W_PLANE1[{FRAME_H*N_WORDS}] = {{
{plane1_str}
}};

static const UWORD SHIP_W_PLANE2[{FRAME_H*N_WORDS}] = {{
{plane2_str}
}};

static const UWORD SHIP_W_PLANE3[{FRAME_H*N_WORDS}] = {{
{plane3_str}
}};

static const UWORD* const SHIP_W_PLANES[4] = {{
    SHIP_W_PLANE0, SHIP_W_PLANE1, SHIP_W_PLANE2, SHIP_W_PLANE3
}};
"""

OUT.write_text(content, encoding="ascii")
print(f"Escrit {OUT}")
print()
print("Compila i prova. Si la nau es veu be amb el foc apretat pero")
print("vol diferent amb el foc no apretat, tambe podem re-exportar la")
print("altra polaritat fent una nova versio de l'export que")
print("intercanvi lo<->hi.")
