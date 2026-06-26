#!/usr/bin/env python3
import os
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PNG_A = os.path.join(ROOT, "sprites", "Sprite-Laser_ba.png")
PNG_B = os.path.join(ROOT, "sprites", "Sprite-Laser_b.png")
OUT_H = os.path.join(ROOT, "gfx_enemy_laser48.h")

def process_png(filepath, is_polarity_b=False):
    img = Image.open(filepath)
    w, h = img.size
    # Expect 48x48
    if w != 48 or h != 48:
        raise ValueError(f"Image {filepath} must be 48x48 pixels, got {w}x{h}")
        
    mask = []
    hi = []
    lo = []
    
    for y in range(48):
        m_row = [0, 0, 0]
        h_row = [0, 0, 0]
        l_row = [0, 0, 0]
        
        for x in range(48):
            val = img.getpixel((x, y))
            word_idx = x // 16
            bit_idx = x % 16
            bit = 1 << (15 - bit_idx)
            
            if val > 0:
                m_row[word_idx] |= bit
                if not is_polarity_b:
                    # Polarity A (Blue)
                    # val 1 (White) -> BPL2 (hi)
                    # val 2 (Black) -> BPL2+BPL6 (hi+lo)
                    # val 3 (Blue)  -> BPL6 (lo)
                    if val == 1:
                        h_row[word_idx] |= bit
                    elif val == 2:
                        h_row[word_idx] |= bit
                        l_row[word_idx] |= bit
                    elif val == 3:
                        l_row[word_idx] |= bit
                else:
                    # Polarity B (Red)
                    # val 1 (White) -> BPL2 (hi)
                    # val 2 (Black) -> BPL4 (lo)
                    # val 3 (Red)   -> BPL2+BPL4 (hi+lo)
                    if val == 1:
                        h_row[word_idx] |= bit
                    elif val == 2:
                        l_row[word_idx] |= bit
                    elif val == 3:
                        h_row[word_idx] |= bit
                        l_row[word_idx] |= bit
                        
        mask.extend(m_row)
        hi.extend(h_row)
        lo.extend(l_row)
        
    return mask, hi, lo

def main():
    if not os.path.exists(PNG_A):
        print(f"Error: {PNG_A} not found!")
        return
    if not os.path.exists(PNG_B):
        print(f"Error: {PNG_B} not found!")
        return

    print("Processing Sprite-Laser_ba.png (Polarity A)...")
    mask_a, hi_a, lo_a = process_png(PNG_A, is_polarity_b=False)
    
    print("Processing Sprite-Laser_b.png (Polarity B)...")
    mask_b, hi_b, lo_b = process_png(PNG_B, is_polarity_b=True)

    lines = []
    lines.append("#pragma once")
    lines.append("#include <exec/types.h>")
    lines.append("")
    lines.append("// Auto-generated from sprites/Sprite-Laser_ba.png and Sprite-Laser_b.png")
    lines.append("#define ENEMY_LASER48_W 48")
    lines.append("#define ENEMY_LASER48_H 48")
    lines.append("")
    
    # Polarity A Arrays
    lines.append("static CHIP_RAM_DATA UWORD g_EnemyLaser48Mask[144] = {")
    for i in range(0, 144, 3):
        lines.append(f"    0x{mask_a[i]:04X},0x{mask_a[i+1]:04X},0x{mask_a[i+2]:04X},")
    lines.append("};")
    lines.append("")

    lines.append("static CHIP_RAM_DATA UWORD g_EnemyLaser48Hi[144] = {")
    for i in range(0, 144, 3):
        lines.append(f"    0x{hi_a[i]:04X},0x{hi_a[i+1]:04X},0x{hi_a[i+2]:04X},")
    lines.append("};")
    lines.append("")

    lines.append("static CHIP_RAM_DATA UWORD g_EnemyLaser48Lo[144] = {")
    for i in range(0, 144, 3):
        lines.append(f"    0x{lo_a[i]:04X},0x{lo_a[i+1]:04X},0x{lo_a[i+2]:04X},")
    lines.append("};")
    lines.append("")

    # Polarity B (Inverted) Arrays
    lines.append("static CHIP_RAM_DATA UWORD g_EnemyLaser48InvMask[144] = {")
    for i in range(0, 144, 3):
        lines.append(f"    0x{mask_b[i]:04X},0x{mask_b[i+1]:04X},0x{mask_b[i+2]:04X},")
    lines.append("};")
    lines.append("")

    lines.append("static CHIP_RAM_DATA UWORD g_EnemyLaser48InvHi[144] = {")
    for i in range(0, 144, 3):
        lines.append(f"    0x{hi_b[i]:04X},0x{hi_b[i+1]:04X},0x{hi_b[i+2]:04X},")
    lines.append("};")
    lines.append("")

    lines.append("static CHIP_RAM_DATA UWORD g_EnemyLaser48InvLo[144] = {")
    for i in range(0, 144, 3):
        lines.append(f"    0x{lo_b[i]:04X},0x{lo_b[i+1]:04X},0x{lo_b[i+2]:04X},")
    lines.append("};")
    lines.append("")

    with open(OUT_H, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
        
    print(f"Wrote {OUT_H}")

if __name__ == "__main__":
    main()
