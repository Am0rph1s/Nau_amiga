#pragma once
#include <exec/types.h>

extern void WaitBlitter(void);
extern void BlitterClear(void* mem, ULONG bytes);
extern void BlitterClearArea(void* addr, int w, int h, int dmod);
extern void ClearGameAreaAsm(void* screen_mem);
extern void DrawParByteBpl0Asm(void* screen_mem, int xb, const UWORD* tile, int scroll, int half);
extern void DrawBob16AlignedAsm(void* screen_mem, const UWORD* mask, const UWORD* data, int xword, int y, int colorMask, int rows);
extern void ClearAndParallaxAsm(void* screen_mem, const UWORD* tileSolid, const UWORD* tileDeco, const short* scrolls);
extern void DrawBorderAsm(UBYTE* screen_mem, const UBYTE* border_data, const UBYTE* border_mirror_data, int scroll_y);
extern int DrawBob32d2Asm(UBYTE* screen_mem, const UWORD* mask,
                           const UWORD* dataHi, const UWORD* dataLo,
                           short x, short y, UBYTE planeHi, UBYTE planeLo);
