#!/usr/bin/env python3
import os
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PNG_A = os.path.join(ROOT, "sprites", "Sprite-fast_A.png")
PNG_B = os.path.join(ROOT, "sprites", "Sprite-fast_b.png")
OUT_H = os.path.join(ROOT, "gfx_enemy_fast16.h")

def main():
    if not os.path.exists(PNG_A):
        print(f"Error: {PNG_A} not found!")
        return
    if not os.path.exists(PNG_B):
        print(f"Error: {PNG_B} not found!")
        return

    # Process A
    img_a = Image.open(PNG_A)
    w_a, h_a = img_a.size
    num_frames_a = w_a // 16

    # Process B
    img_b = Image.open(PNG_B)
    w_b, h_b = img_b.size
    num_frames_b = w_b // 16

    num_frames = min(num_frames_a, num_frames_b)
    print(f"Importing {num_frames} frames (A: {num_frames_a}, B: {num_frames_b})")

    frames_mask = []
    frames_hi = []
    frames_lo = []

    frames_inv_mask = []
    frames_inv_hi = []
    frames_inv_lo = []

    for f in range(num_frames):
        m_a = []
        h_a = []
        l_a = []

        m_b = []
        h_b = []
        l_b = []

        for y in range(16):
            # A (Planes 1 and 5: BPL2 and BPL6)
            # val 1 (White) -> BPL2 (hi)
            # val 2 (Black) -> BPL2+BPL6 (hi+lo)
            # val 3 (Blue)  -> BPL6 (lo)
            w_m = 0
            w_h = 0
            w_l = 0
            for x in range(16):
                val = img_a.getpixel((f * 16 + x, y))
                if val > 0:
                    w_m |= (1 << (15 - x))
                    if val == 1: # White
                        w_h |= (1 << (15 - x))
                    elif val == 2: # Black
                        w_h |= (1 << (15 - x))
                        w_l |= (1 << (15 - x))
                    elif val == 3: # Blue
                        w_l |= (1 << (15 - x))

            m_a.append(w_m)
            h_a.append(w_h)
            l_a.append(w_l)

            # B (Planes 1 and 3: BPL2 and BPL4)
            # val 1 (White) -> BPL2 (hi)
            # val 2 (Black) -> BPL4 (lo)
            # val 3 (Red)   -> BPL2+BPL4 (hi+lo)
            w_im = 0
            w_ih = 0
            w_il = 0
            for x in range(16):
                val = img_b.getpixel((f * 16 + x, y))
                if val > 0:
                    w_im |= (1 << (15 - x))
                    if val == 1: # White
                        w_ih |= (1 << (15 - x))
                    elif val == 2: # Black
                        w_il |= (1 << (15 - x))
                    elif val == 3: # Red
                        w_ih |= (1 << (15 - x))
                        w_il |= (1 << (15 - x))

            m_b.append(w_im)
            h_b.append(w_ih)
            l_b.append(w_il)

        frames_mask.append(m_a)
        frames_hi.append(h_a)
        frames_lo.append(l_a)

        frames_inv_mask.append(m_b)
        frames_inv_hi.append(h_b)
        frames_inv_lo.append(l_b)

    lines = []
    lines.append("#pragma once")
    lines.append("#include <exec/types.h>")
    lines.append("")
    lines.append(f"// Auto-generated from sprites/Sprite-fast_A.png and Sprite-fast_b.png")
    lines.append(f"#define ENEMY_FAST_NUM_FRAMES {num_frames}")
    lines.append("")

    for f in range(num_frames):
        lines.append(f"// --- FRAME {f} ---")
        lines.append(f"static CHIP_RAM_DATA UWORD g_EnemyFast16Mask_f{f}[16] = {{")
        for i in range(0, 16, 4):
            lines.append(f"    0x{frames_mask[f][i]:04X}, 0x{frames_mask[f][i+1]:04X}, 0x{frames_mask[f][i+2]:04X}, 0x{frames_mask[f][i+3]:04X},")
        lines.append("};")
        
        lines.append(f"static CHIP_RAM_DATA UWORD g_EnemyFast16Hi_f{f}[16] = {{")
        for i in range(0, 16, 4):
            lines.append(f"    0x{frames_hi[f][i]:04X}, 0x{frames_hi[f][i+1]:04X}, 0x{frames_hi[f][i+2]:04X}, 0x{frames_hi[f][i+3]:04X},")
        lines.append("};")
        
        lines.append(f"static CHIP_RAM_DATA UWORD g_EnemyFast16Lo_f{f}[16] = {{")
        for i in range(0, 16, 4):
            lines.append(f"    0x{frames_lo[f][i]:04X}, 0x{frames_lo[f][i+1]:04X}, 0x{frames_lo[f][i+2]:04X}, 0x{frames_lo[f][i+3]:04X},")
        lines.append("};")

        lines.append(f"static CHIP_RAM_DATA UWORD g_EnemyFast16InvMask_f{f}[16] = {{")
        for i in range(0, 16, 4):
            lines.append(f"    0x{frames_inv_mask[f][i]:04X}, 0x{frames_inv_mask[f][i+1]:04X}, 0x{frames_inv_mask[f][i+2]:04X}, 0x{frames_inv_mask[f][i+3]:04X},")
        lines.append("};")

        lines.append(f"static CHIP_RAM_DATA UWORD g_EnemyFast16InvHi_f{f}[16] = {{")
        for i in range(0, 16, 4):
            lines.append(f"    0x{frames_inv_hi[f][i]:04X}, 0x{frames_inv_hi[f][i+1]:04X}, 0x{frames_inv_hi[f][i+2]:04X}, 0x{frames_inv_hi[f][i+3]:04X},")
        lines.append("};")
        
        lines.append(f"static CHIP_RAM_DATA UWORD g_EnemyFast16InvLo_f{f}[16] = {{")
        for i in range(0, 16, 4):
            lines.append(f"    0x{frames_inv_lo[f][i]:04X}, 0x{frames_inv_lo[f][i+1]:04X}, 0x{frames_inv_lo[f][i+2]:04X}, 0x{frames_inv_lo[f][i+3]:04X},")
        lines.append("};")
        lines.append("")

    lines.append("// Arrays of frames pointers")
    lines.append("static const UWORD* const g_EnemyFast16Mask_Frames[] = {")
    for f in range(num_frames):
        lines.append(f"    g_EnemyFast16Mask_f{f},")
    lines.append("};")
    
    lines.append("static const UWORD* const g_EnemyFast16Hi_Frames[] = {")
    for f in range(num_frames):
        lines.append(f"    g_EnemyFast16Hi_f{f},")
    lines.append("};")
    
    lines.append("static const UWORD* const g_EnemyFast16Lo_Frames[] = {")
    for f in range(num_frames):
        lines.append(f"    g_EnemyFast16Lo_f{f},")
    lines.append("};")

    lines.append("static const UWORD* const g_EnemyFast16InvMask_Frames[] = {")
    for f in range(num_frames):
        lines.append(f"    g_EnemyFast16InvMask_f{f},")
    lines.append("};")

    lines.append("static const UWORD* const g_EnemyFast16InvHi_Frames[] = {")
    for f in range(num_frames):
        lines.append(f"    g_EnemyFast16InvHi_f{f},")
    lines.append("};")
    
    lines.append("static const UWORD* const g_EnemyFast16InvLo_Frames[] = {")
    for f in range(num_frames):
        lines.append(f"    g_EnemyFast16InvLo_f{f},")
    lines.append("};")

    with open(OUT_H, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
        
    print(f"Wrote {OUT_H}")

if __name__ == "__main__":
    main()
