#!/usr/bin/env python3
import os
import re
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
FAST16_H = os.path.join(ROOT, "gfx_enemy_fast16.h")
BASIC24_H = os.path.join(ROOT, "gfx_enemy_basic24.h")
OUT_DIR = os.path.join(ROOT, "sprites")
os.makedirs(OUT_DIR, exist_ok=True)

def parse_uwords(text):
    nums = re.findall(r'0x[0-9A-Fa-f]+', text)
    return [int(n, 16) for n in nums]

def get_array(filepath, name):
    with open(filepath, "r", encoding="utf-8") as f:
        src = f.read()
    m = re.search(r'UWORD\s+' + name + r'\[\d*\]\s*=\s*\{([^}]+)\}', src)
    if not m:
        # Try static const UWORD or other variations
        m = re.search(r'static\s+(?:CHIP_RAM_DATA\s+)?UWORD\s+' + name + r'\[\d*\]\s*=\s*\{([^}]+)\}', src)
    if not m:
        raise ValueError(f"Array {name} not found in {filepath}")
    return parse_uwords(m.group(1))

# Palette: 
# 0 = transparent (0, 0, 0, 0)
# 1 = white (255, 255, 255, 255)
# 2 = black (0, 0, 0, 255)
# 3 = light grey (170, 170, 170, 255)
PALETTE = {
    0: (0, 0, 0, 0),
    1: (255, 255, 255, 255),
    2: (0, 0, 0, 255),
    3: (170, 170, 170, 255)
}

def export_sprite_16(mask, hi, lo, filename):
    img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    pix = img.load()
    for y in range(16):
        m_word = mask[y]
        hi_word = hi[y]
        lo_word = lo[y]
        for x in range(16):
            bit = 1 << (15 - x)
            if not (m_word & bit):
                color_idx = 0
            else:
                hi_bit = 1 if (hi_word & bit) else 0
                lo_bit = 1 if (lo_word & bit) else 0
                color_idx = (lo_bit << 1) | hi_bit
            pix[x, y] = PALETTE[color_idx]
    
    out_path = os.path.join(OUT_DIR, filename)
    img.save(out_path)
    print(f"Exported 16x16: {out_path}")

def export_sprite_24(mask, hi, lo, filename):
    # 24 rows, 2 words per row (48 words total)
    img = Image.new("RGBA", (24, 24), (0, 0, 0, 0))
    pix = img.load()
    for y in range(24):
        m_words = (mask[y*2], mask[y*2 + 1])
        hi_words = (hi[y*2], hi[y*2 + 1])
        lo_words = (lo[y*2], lo[y*2 + 1])
        
        # 24 pixels total
        for x in range(24):
            word_idx = x // 16
            bit_idx = x % 16
            bit = 1 << (15 - bit_idx)
            
            if not (m_words[word_idx] & bit):
                color_idx = 0
            else:
                hi_bit = 1 if (hi_words[word_idx] & bit) else 0
                lo_bit = 1 if (lo_words[word_idx] & bit) else 0
                color_idx = (lo_bit << 1) | hi_bit
            pix[x, y] = PALETTE[color_idx]
            
    out_path = os.path.join(OUT_DIR, filename)
    img.save(out_path)
    print(f"Exported 24x24: {out_path}")

def main():
    print("Parsing fast16 arrays...")
    fast16_mask = get_array(FAST16_H, "g_EnemyFast16Mask")
    fast16_hi = get_array(FAST16_H, "g_EnemyFast16Hi")
    fast16_lo = get_array(FAST16_H, "g_EnemyFast16Lo")
    fast16_inv_hi = get_array(FAST16_H, "g_EnemyFast16InvHi")
    fast16_inv_lo = get_array(FAST16_H, "g_EnemyFast16InvLo")
    
    export_sprite_16(fast16_mask, fast16_hi, fast16_lo, "enemy_fast16.png")
    export_sprite_16(fast16_mask, fast16_inv_hi, fast16_inv_lo, "enemy_fast16_inv.png")
    
    print("Parsing basic24 arrays...")
    basic24_mask = get_array(BASIC24_H, "g_EnemyBasic24Mask")
    basic24_hi = get_array(BASIC24_H, "g_EnemyBasic24Hi")
    basic24_lo = get_array(BASIC24_H, "g_EnemyBasic24Lo")
    basic24_inv_hi = get_array(BASIC24_H, "g_EnemyBasic24InvHi")
    basic24_inv_lo = get_array(BASIC24_H, "g_EnemyBasic24InvLo")
    
    export_sprite_24(basic24_mask, basic24_hi, basic24_lo, "enemy_basic24.png")
    export_sprite_24(basic24_mask, basic24_inv_hi, basic24_inv_lo, "enemy_basic24_inv.png")

if __name__ == "__main__":
    main()
