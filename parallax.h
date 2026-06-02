#pragma once
#include <exec/types.h>

// Parallax scroll: two rocky wall columns on each side of the game area
// Left wall:  x=0..15  (16px wide)
// Right wall: x=272..287 (16px wide, before HUD)
// Two layers per wall: back (slow, dark) and front (fast, light)
// Tile height: 64 rows, tiling vertically over 256 lines

#define PAR_TILE_H   64      // tile height in pixels
#define PAR_TILE_W   16      // tile width in pixels (1 word)
#define PAR_SPEED_BACK  1    // pixels per frame (back layer)
#define PAR_SPEED_FRONT 2    // pixels per frame (front layer)

// Two scroll offsets (0..PAR_TILE_H-1)
extern short g_ParBack;   // back layer scroll position
extern short g_ParFront;  // front layer scroll position

// Tile data: 64 rows x 1 word each, two tiles (back=dark, front=light)
// Generated procedurally at init
extern UWORD g_TileBack[PAR_TILE_H];   // color index per pixel via mask
extern UWORD g_TileFront[PAR_TILE_H];  // front tile

void ParallaxInit(void);
void ParallaxUpdate(void);
void ParallaxDraw(UBYTE* screen_mem);
