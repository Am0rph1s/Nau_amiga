"""Exporta la nau actual (gfx_ship_white.h) com a PNG del segon estat
(polaritat negra = foc apretat) perquè es pugui editar.

Cada pixel del PNG té un color que correspon a la composicio 2-BPL en
polaritat negra:
    transparent (0,0)        -> body=0, outline=0
    FOSC         (lo=1,hi=0)  -> body=1 (era lo a BPL4, ara pintat fosc)
    BLANC        (lo=0,hi=1)  -> outline=1 (era hi a BPL2, ara pintat blanc)
    GRIS CLAR    (lo=1,hi=1)  -> interior=1+1 (highlight, slot 11)

Aquests colors corresponen aproximadament als valors de paleta Amiga:
    FOSC         = 0x0111 (~RGB 17,17,17)
    BLANC        = 0x0FFF (~RGB 255,255,255)
    GRIS CLAR    = 0x0CCC (~RGB 204,204,204)
"""
import re
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "gfx_ship_white.h"
OUT = ROOT / "sprites" / "nau_fire_held.png"

FRAME_W = 32
FRAME_H = 24

COLOR_TRANSPARENT = (0, 0, 0, 0)
COLOR_DARK         = (0x01, 0x01, 0x01, 0xFF)   # slot 10 (fosc)
COLOR_WHITE        = (0x0F, 0x0F, 0x0F, 0xFF)   # slot 9  (blanc)
COLOR_LIGHT_GREY   = (0x0C, 0x0C, 0x0C, 0xFF)   # slot 11 (gris clar, highlight)

# Llegeix gfx_ship_white.h i extreu els 4 arrays de bitplanes
with open(SRC) as f:
    content = f.read()

def extract(name):
    m = re.search(rf"static const UWORD {name}\[\d+\] = \{{([^}}]+)\}};", content, re.DOTALL)
    if not m:
        raise SystemExit(f"No trobo l'array {name} a {SRC}")
    return [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", m.group(1))]

p0 = extract("SHIP_W_PLANE0")
p1 = extract("SHIP_W_PLANE1")
p2 = extract("SHIP_W_PLANE2")
p3 = extract("SHIP_W_PLANE3")

assert len(p0) == 48, f"S'esperaven 48 entrades, tenim {len(p0)}"

img = Image.new("RGBA", (FRAME_W, FRAME_H), COLOR_TRANSPARENT)
px = img.load()

for y in range(FRAME_H):
    w_p0 = (p0[y*2], p0[y*2+1])
    w_p1 = (p1[y*2], p1[y*2+1])
    w_p2 = (p2[y*2], p2[y*2+1])
    w_p3 = (p3[y*2], p3[y*2+1])

    for word_i in range(2):
        for bit in range(16):
            x = word_i * 16 + bit
            b0 = (w_p0[word_i] >> (15 - bit)) & 1
            b1 = (w_p1[word_i] >> (15 - bit)) & 1
            b2 = (w_p2[word_i] >> (15 - bit)) & 1
            b3 = (w_p3[word_i] >> (15 - bit)) & 1
            lo = b0 | b1
            hi = b2 | b3
            if lo == 0 and hi == 0:
                c = COLOR_TRANSPARENT
            elif lo == 1 and hi == 0:
                c = COLOR_DARK
            elif lo == 0 and hi == 1:
                c = COLOR_WHITE
            else:  # lo==1 and hi==1
                c = COLOR_LIGHT_GREY
            px[x, y] = c

img.save(OUT)
print(f"Escrit {OUT} ({FRAME_W}x{FRAME_H})")
print()
print("PALETA DEL PNG:")
print(f"  Transparent = RGBA{COLOR_TRANSPARENT}  <- fons (no es dibuixa)")
print(f"  Fosc        = RGBA{COLOR_DARK}        <- era el cos en polaritat negra")
print(f"  Blanc       = RGBA{COLOR_WHITE}       <- era el contorn en polaritat negra")
print(f"  Gris clar   = RGBA{COLOR_LIGHT_GREY}  <- era l'interior (highlight)")
print()
print("Edita el PNG i desa'l. Despres executare 'ship_import_fireheld.py'")
print("per regenerar gfx_ship_white.h amb el teu disseny.")
