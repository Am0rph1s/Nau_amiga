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

def color_to_planes(px):
    """Classify pixel into 3 bitplanes:
      plane0 -> BPL2 (PF2 bit 0, color 9):  white pixels
      plane1 -> BPL6 (PF2 bit 2, color 12): colored pixels (purple, grey, red)
      plane2 -> BPL4 (PF2 bit 1, color 10): dark/black pixels
    This works for both polarities: white ship + coloured detail + dark outline,
    or black ship + red detail + white accent.
    """
    if len(px) == 4 and px[3] < 16:
        return (0, 0, 0)
    r, g, b = px[0], px[1], px[2]
    if r > 200 and g > 200 and b > 200:
        return (1, 0, 0)  # white -> plane0 (BPL2)
    if r < 60 and g < 60 and b < 60:
        return (0, 0, 1)  # dark -> plane2 (BPL4)
    return (0, 1, 0)      # coloured -> plane1 (BPL6)

def png_to_planes(path):
    img = Image.open(path).convert("RGBA")
    if img.size != (FRAME_W, FRAME_H):
        raise SystemExit(f"{path}: mida {img.size}, esperada ({FRAME_W}, {FRAME_H})")

    mask_words  = [0] * (FRAME_H * N_WORDS)
    plane0_words = [0] * (FRAME_H * N_WORDS)  # white -> BPL2
    plane1_words = [0] * (FRAME_H * N_WORDS)  # coloured -> BPL6
    plane2_words = [0] * (FRAME_H * N_WORDS)  # dark -> BPL4
    plane3_words = [0] * (FRAME_H * N_WORDS)  # unused

    for y in range(FRAME_H):
        for word_i in range(N_WORDS):
            word_idx = y * N_WORDS + word_i
            for bit in range(16):
                x = word_i * 16 + bit
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
// Auto-generated from sprites/nau_fire_polarity_a.png (colorMode=0)
// and sprites/nau_fire_polarity_B.png (colorMode=1).
//
// 3-plane mapping (white | coloured | dark):
//   plane0 -> BPL2 (PF2 bit 0, color 9):  white pixels
//   plane1 -> BPL6 (PF2 bit 2, color 12): coloured pixels (purple, grey, red)
//   plane2 -> BPL4 (PF2 bit 1, color 10): dark/black pixels
//   plane3 -> unused (zero)
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
print()
print("IMPORTANT: tambe cal modificar DrawShipAnim perque trii entre")
print("SHIP_W_A_PLANES i SHIP_W_B_PLANES segons colorMode (en lloc del swap).")
