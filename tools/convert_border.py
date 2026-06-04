"""Convert border PNG (64xN) to 3-bitplane PF2 tile data with color remapping."""
from PIL import Image
import sys, os

# PF2 color slot mapping for border (old index -> PF2 slot)
# index 0 = transparent -> PF2 color 0 (slot 8, no bits)
# index 1 = dark accent -> PF2 color 5 (slot 13, bits 101 = planes 0+2)
# index 2 = light brown -> PF2 color 6 (slot 14, bits 110 = planes 1+2)
# index 3 = dark brown  -> PF2 color 7 (slot 15, bits 111 = all 3)
COLOR_REMAP = {
    0: 0,  # transparent (slot 8)
    1: 5,  # dark accent (slot 13)
    2: 6,  # light brown (slot 14)
    3: 7,  # dark brown (slot 15)
}

TILE_W, TILE_H = 16, 16

def pixels_to_planar_remapped(pixels, w, h, remap):
    """Convert indexed pixels to 3 planar bitplanes with color remapping.
    Returns 3 bytearrays (BPL2, BPL4, BPL6 for PF2)."""
    plane_size = (w // 8) * h
    planes = [bytearray(plane_size) for _ in range(3)]
    for y in range(h):
        for x in range(w):
            idx = pixels[y * w + x]
            color = remap.get(idx, 0)
            byte_off = y * (w // 8) + (x // 8)
            bit = 7 - (x & 7)
            if color & 1: planes[0][byte_off] |= (1 << bit)  # BPL2
            if color & 2: planes[1][byte_off] |= (1 << bit)  # BPL4
            if color & 4: planes[2][byte_off] |= (1 << bit)  # BPL6
    return planes

def to_amiga_color(rgb):
    r4 = (rgb[0] >> 4) & 0xF
    g4 = (rgb[1] >> 4) & 0xF
    b4 = (rgb[2] >> 4) & 0xF
    return (r4 << 8) | (g4 << 4) | b4

# Bit reversal table for mirroring
def rev8(b):
    return int(f'{b:08b}'[::-1], 2)
BIT_REV = [rev8(i) for i in range(256)]

def main():
    png_path = os.path.join(os.path.dirname(__file__), '..', 'escenari', 'border desert.png')
    img = Image.open(png_path)
    if img.mode != 'P':
        print("ERROR: border must be indexed PNG")
        sys.exit(1)

    w, h = img.size
    print(f"Border: {w}x{h}")

    pal = img.getpalette()
    pixels = list(img.getdata())

    # Convert to planar with remapping
    planes = pixels_to_planar_remapped(pixels, w, h, COLOR_REMAP)

    # Generate mirrored planes
    row_bytes = w // 8  # Should be 4 for 64px? No, 64/8 = 8
    planes_mirror = [bytearray(len(planes[0])) for _ in range(3)]
    for p in range(3):
        for y in range(h):
            row_src = y * row_bytes
            for x in range(row_bytes):
                planes_mirror[p][row_src + x] = BIT_REV[planes[p][row_src + (row_bytes - 1 - x)]]

    # Flatten all plane data
    border_data = b''
    for p in range(3):
        border_data += bytes(planes[p])
    border_mirror_data = b''
    for p in range(3):
        border_mirror_data += bytes(planes_mirror[p])

    # Palette reference
    amiga_pal = {}
    for i in range(len(pal)//3):
        r,g,b = pal[i*3], pal[i*3+1], pal[i*3+2]
        amiga_pal[i] = to_amiga_color((r,g,b))

    # Write .h
    out_path = os.path.join(os.path.dirname(__file__), '..', 'gfx_border.h')
    with open(out_path, 'w') as f:
        f.write(f'// Auto-generated from escenari/{os.path.basename(png_path)}\n')
        f.write(f'// {w}x{h}, 3-bitplane PF2 with color remapping\n')
        f.write('#pragma once\n')
        f.write('#include <exec/types.h>\n\n')

        f.write(f'#define BORDER_W          {w}\n')
        f.write(f'#define BORDER_H          {h}\n')
        f.write(f'#define BORDER_ROW_BYTES  {row_bytes}\n')
        f.write(f'#define BORDER_PLANE_SIZE {len(planes[0])}\n\n')

        # Original PNG palette (reference)
        f.write(f'// PNG palette -> PF2 slot mapping\n')
        f.write(f'// idx 0 ({amiga_pal[0]:04X}) -> PF2 trans  (slot  8)\n')
        f.write(f'// idx 1 ({amiga_pal[1]:04X}) -> PF2 color5 (slot 13)\n')
        f.write(f'// idx 2 ({amiga_pal[2]:04X}) -> PF2 color6 (slot 14)\n')
        f.write(f'// idx 3 ({amiga_pal[3]:04X}) -> PF2 color7 (slot 15)\n\n')

        # Left border data (3 planes sequential)
        f.write(f'static const UBYTE border_data[{len(border_data)}] = {{\n')
        for i in range(0, len(border_data), 16):
            f.write('    ')
            for j in range(i, min(i+16, len(border_data))):
                f.write(f'0x{border_data[j]:02X},')
            f.write('\n')
        f.write('};\n\n')

        # Right border (mirrored) data
        f.write(f'static const UBYTE border_mirror_data[{len(border_mirror_data)}] = {{\n')
        for i in range(0, len(border_mirror_data), 16):
            f.write('    ')
            for j in range(i, min(i+16, len(border_mirror_data))):
                f.write(f'0x{border_mirror_data[j]:02X},')
            f.write('\n')
        f.write('};\n')

    print(f"Written: {out_path} ({len(border_data)} bytes per side)")

if __name__ == '__main__':
    main()
