#!/usr/bin/env python3
"""
Extracts the current shot sprites from gfx.h as editable PNGs.

--raw : export raw 4xN PNGs (each pixel = 1 bit, actual size).

The reverse direction is in png_to_shots.py.
"""
from PIL import Image, ImageDraw
import os
import sys

SCALE = 16
SPRITE_W = 16
PAD = 12
LEGEND_H = 60

COLOR_BODY = (255, 255, 255)
COLOR_ACCENT = (255, 0, 0)

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "shots")
os.makedirs(OUT_DIR, exist_ok=True)

def render_raw(mask, body, accent, filename, body_color=(0, 204, 255), accent_color=(255, 0, 0)):
    h = len(mask)
    w = 4
    img = Image.new('RGB', (w, h), (0, 0, 0))
    for y in range(h):
        for x in range(w):
            bit = 0x8000 >> x
            if mask[y] & bit:
                if accent[y] & bit:
                    img.putpixel((x, y), accent_color)
                elif body[y] & bit:
                    img.putpixel((x, y), body_color)
    path = os.path.join(OUT_DIR, filename)
    img.save(path)
    print(f"  {path}")

def render_sprite(mask, body, accent, filename, subtitle):
    h = len(mask)
    img_w = SPRITE_W * SCALE + PAD * 2
    img_h = h * SCALE + PAD * 2 + LEGEND_H
    img = Image.new('RGB', (img_w, img_h), (40, 40, 40))
    draw = ImageDraw.Draw(img)
    for y in range(h + 1):
        draw.line([(PAD, PAD + y * SCALE), (PAD + SPRITE_W * SCALE, PAD + y * SCALE)], fill=(80, 80, 80))
    for x in range(SPRITE_W + 1):
        draw.line([(PAD + x * SCALE, PAD), (PAD + x * SCALE, PAD + h * SCALE)], fill=(80, 80, 80))
    for y in range(h):
        for x in range(SPRITE_W):
            bit = 0x8000 >> x
            if mask[y] & bit:
                color = COLOR_ACCENT if (accent[y] & bit) else COLOR_BODY
                x0 = PAD + x * SCALE
                y0 = PAD + y * SCALE
                draw.rectangle([x0 + 1, y0 + 1, x0 + SCALE - 2, y0 + SCALE - 2], fill=color)
    draw.text((PAD, PAD + h * SCALE + 12), f"Player shot  ({subtitle})", fill=(255, 255, 255))
    path = os.path.join(OUT_DIR, filename)
    img.save(path)
    print(f"  {path}")

# Hardcoded data matching current gfx.h
PLAYER_OLD_MASK   = [0x6000,0xf000,0xf000,0xf000,0x6000,0x6000,0x6000,0x0]
PLAYER_OLD_BODY   = [0x6000,0x9000,0x9000,0x9000,0x6000,0x6000,0x6000,0x0]
PLAYER_OLD_ACCENT = [0x0,   0x6000,0x6000,0x6000,0x0,   0x0,   0x0,   0x0]

ENEMY_OLD_MASK   = [0x0000,0x03C0,0x07E0,0x03C0,0x0000,0x0000,0x0000,0x0000]
ENEMY_OLD_BODY   = [0x0000,0x03C0,0x0660,0x03C0,0x0000,0x0000,0x0000,0x0000]
ENEMY_OLD_ACCENT = [0x0000,0x0000,0x0180,0x0000,0x0000,0x0000,0x0000,0x0000]

# 4x16 data
SHOT16W_MASK   = [0x6000,0xF000,0xF000,0xF000,0xF000,0xF000,0xF000,0xF000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000]
SHOT16W_BODY   = [0x6000,0x9000,0x9000,0x9000,0x9000,0x9000,0x9000,0x9000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000]
SHOT16W_ACCENT = [0x0000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000]

SHOT16B_MASK   = [0x6000,0x9000,0x9000,0x9000,0x9000,0x9000,0x9000,0x9000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000]
SHOT16B_BODY   = [0x6000,0x9000,0x9000,0x9000,0x9000,0x9000,0x9000,0x9000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000]
SHOT16B_ACCENT = [0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000]

if __name__ == "__main__":
    raw = "--raw" in sys.argv
    print("Extracting shot sprites to:", os.path.abspath(OUT_DIR))

    if raw:
        print("\nRaw 4x16 player shots (blue=body, red=accent):")
        render_raw(SHOT16W_MASK, SHOT16W_BODY, SHOT16W_ACCENT, "shot_player_white_16.png",
                   body_color=(0, 204, 255), accent_color=(255, 255, 255))
        render_raw(SHOT16B_MASK, SHOT16B_BODY, SHOT16B_ACCENT, "shot_player_black_16.png",
                   body_color=(255, 0, 0), accent_color=(0, 0, 0))
    else:
        print("\nPlayer shot (4x8 effective, 16x8 grid):")
        render_sprite(PLAYER_OLD_MASK, PLAYER_OLD_BODY, PLAYER_OLD_ACCENT, "shot_player_white.png", "white polarity")
        render_sprite(PLAYER_OLD_MASK, PLAYER_OLD_BODY, PLAYER_OLD_ACCENT, "shot_player_black.png", "black polarity")
        print("\nEnemy shot (10x4 effective, 16x4 grid):")
        render_sprite(ENEMY_OLD_MASK, ENEMY_OLD_BODY, ENEMY_OLD_ACCENT, "shot_enemy_white.png", "white polarity")
        render_sprite(ENEMY_OLD_MASK, ENEMY_OLD_BODY, ENEMY_OLD_ACCENT, "shot_enemy_black.png", "black polarity")

    print(f"\nDone. Edit the PNGs and run png_to_shots.py{' --raw' if raw else ''} to convert them back to C arrays.")
