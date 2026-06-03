"""Convert a 256xN indexed PNG to Amiga tilemap + planar tileset."""
from PIL import Image
import sys, os

TILE_W, TILE_H = 16, 16
NUM_BPL = 3  # PF1 has 3 bitplanes (3+3 dual-playfield)

def to_amiga_color(rgb):
    """Convert 8-bit RGB tuple to Amiga 12-bit 0x0RGB."""
    r4 = (rgb[0] >> 4) & 0xF
    g4 = (rgb[1] >> 4) & 0xF
    b4 = (rgb[2] >> 4) & 0xF
    return (r4 << 8) | (g4 << 4) | b4

def pixels_to_planar(pixels, w, h, nplanes):
    """Convert indexed pixel data to planar bitplane format.
    Returns list of nplanes bytearrays, each (w/8 * h) bytes.
    """
    plane_size = (w // 8) * h
    planes = [bytearray(plane_size) for _ in range(nplanes)]
    for y in range(h):
        for x in range(w):
            byte_off = y * (w // 8) + (x // 8)
            bit = 7 - (x & 7)
            color_idx = pixels[y * w + x]
            for p in range(nplanes):
                if color_idx & (1 << p):
                    planes[p][byte_off] |= (1 << bit)
    return planes

def main():
    if len(sys.argv) < 2:
        png_path = os.path.join(os.path.dirname(__file__), '..', 'escenari', 'cano_tileset_b.png')
    else:
        png_path = sys.argv[1]

    img = Image.open(png_path)
    if img.mode != 'P':
        img = img.convert('P', palette=Image.Palette.ADAPTIVE, colors=16)
        print(f"Converted to indexed palette ({len(img.getcolors())} colors)")

    w, h = img.size
    print(f"Image: {w}x{h}, mode={img.mode}, colors={len(set(img.getdata()))}")

    if w != 256:
        print(f"WARNING: expected width 256, got {w}")

    palette = img.getpalette()
    num_colors = len(palette) // 3

    # Convert palette to Amiga 12-bit
    amiga_pal = []
    for i in range(num_colors):
        rgb = (palette[i*3], palette[i*3+1], palette[i*3+2])
        amiga_pal.append(to_amiga_color(rgb))

    # Pad to 16 colors
    while len(amiga_pal) < 16:
        amiga_pal.append(0x0000)

    # Extract tiles
    cols = w // TILE_W
    rows = h // TILE_H
    pixels = list(img.getdata())
    tile_data = []  # list of (hash, planar_bytes)
    tile_hashes = {}
    tilemap = []    # indices into tileset

    for ty in range(rows):
        for tx in range(cols):
            # Extract 16x16 tile pixels
            tile_pixels = []
            for py in range(TILE_H):
                y = ty * TILE_H + py
                for px in range(TILE_W):
                    x = tx * TILE_W + px
                    tile_pixels.append(pixels[y * w + x])

            tile_bytes = b''
            planes = pixels_to_planar(tile_pixels, TILE_W, TILE_H, NUM_BPL)
            for p in range(NUM_BPL):
                tile_bytes += bytes(planes[p])

            tile_hash = hash(tile_bytes)
            if tile_hash not in tile_hashes:
                tile_hashes[tile_hash] = len(tile_data)
                tile_data.append(tile_bytes)
            tilemap.append(tile_hashes[tile_hash])

    print(f"Tiles: {len(tile_data)} unique out of {cols*rows} total")
    print(f"Tileset size: {len(tile_data) * len(tile_data[0])} bytes")
    print(f"Tilemap size: {len(tilemap)} bytes ({cols}x{rows})")

    # Write .h file
    out_path = os.path.join(os.path.dirname(__file__), '..', 'gfx_bg_tiles.h')
    name = os.path.splitext(os.path.basename(png_path))[0]

    with open(out_path, 'w') as f:
        f.write('// Auto-generated from escenari/' + os.path.basename(png_path) + '\n')
        f.write(f'// {w}x{h} -> {cols}x{rows} tiles of {TILE_W}x{TILE_H}\n')
        f.write(f'// {len(tile_data)} unique tiles, {NUM_BPL} bitplanes\n')
        f.write('#pragma once\n')
        f.write('#include <exec/types.h>\n\n')

        f.write(f'#define BG_TILESET_SIZE {len(tile_data)}\n')
        f.write(f'#define BG_TILE_W       {TILE_W}\n')
        f.write(f'#define BG_TILE_H       {TILE_H}\n')
        f.write(f'#define BG_TILE_BYTES   {len(tile_data[0])}\n')
        f.write(f'#define BG_MAP_COLS     {cols}\n')
        f.write(f'#define BG_MAP_ROWS     {rows}\n')
        f.write(f'#define BG_MAP_TOTAL    {len(tilemap)}\n')
        f.write(f'#define BG_BPL          {NUM_BPL}\n\n')

        # Palette (16 entries, Amiga 12-bit)
        f.write(f'#define BG_PAL_AMIGA_COUNT {len(amiga_pal)}\n')
        f.write(f'static const UWORD bg_pal_amiga[{len(amiga_pal)}] = {{\n')
        for i, c in enumerate(amiga_pal):
            f.write(f'    0x{c:04X},  // {i}\n')
        f.write('};\n\n')

        # Tileset
        f.write(f'static const UBYTE bg_tiles[{len(tile_data)}][BG_TILE_BYTES] = {{\n')
        for i, t in enumerate(tile_data):
            f.write(f'    /* {i:3d} */ {{')
            for j, b in enumerate(t):
                if j % 16 == 0 and j > 0:
                    f.write('\n                 ')
                f.write(f'0x{b:02X},')
            f.write('},\n')
        f.write('};\n\n')

        # Tilemap (UWORD needed when >255 unique tiles)
        f.write(f'static const UWORD bg_tilemap[BG_MAP_TOTAL] = {{\n')
        for i in range(0, len(tilemap), 16):
            f.write('    ')
            for j in range(i, min(i+16, len(tilemap))):
                f.write(f'0x{tilemap[j]:04X},')
            f.write('\n')
        f.write('};\n\n')

        # PF1 plane offsets within screen_mem (dual-playfield: PF1 = BPL1,3,5)
        f.write('// PF1 bitplane -> screen_mem plane offset (PF1=3 PF2=3)\n')
        f.write(f'static const int bg_pf1_plane_screen[{NUM_BPL}] = {{ 0, 2, 4 }};\n')

    print(f"Written: {out_path}")

if __name__ == '__main__':
    main()
