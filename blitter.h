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
extern int DrawBob16Asm(UBYTE* screen_mem, const UWORD* mask, const UWORD* data,
                         short x, short y, UBYTE colorMask, UWORD rows);
extern int DrawBob32d2Asm(UBYTE* screen_mem, const UWORD* mask,
                           const UWORD* dataHi, const UWORD* dataLo,
                           short x, short y, UBYTE planeHi, UBYTE planeLo);
extern int DrawBob16d2Asm(UBYTE* screen_mem, const UWORD* mask,
                           const UWORD* dataHi, const UWORD* dataLo,
                           short x, short y, UBYTE planeHi, UBYTE planeLo, UWORD rows);

// Entity update routines (68k ASM, see entities.s)
extern void AsmUpdatePlayerShots(void);
extern void AsmUpdateEnemyShots(void);
extern void AsmUpdateEnemies(void);
extern void AsmUpdateExplosions(void);
extern void AsmEnemyFire(void);
