#pragma once
#include <exec/types.h>

extern void ClearGameAreaAsm(void* screen_mem);
extern int DrawBob16Asm(UBYTE* screen_mem, const UWORD* mask, const UWORD* data,
                         short x, short y, UBYTE colorMask, UWORD rows);
extern int DrawBob32d2Asm(UBYTE* screen_mem, const UWORD* mask,
                           const UWORD* dataHi, const UWORD* dataLo,
                           short x, short y, UBYTE planeHi, UBYTE planeLo);
extern int DrawBob16d2Asm(UBYTE* screen_mem, const UWORD* mask,
                           const UWORD* dataHi, const UWORD* dataLo,
                           short x, short y, UBYTE planeHi, UBYTE planeLo, UWORD rows);
extern int DrawForceFieldMaskAsm(void* screen_mem, short x, short y,
                                  const UWORD* mask, int planeIdx);
extern int DrawForceFieldMask2Asm(void* screen_mem, short x, short y,
                                   const UWORD* mask, int planeA, int planeB);

// Entity update routines (68k ASM, see entities.s)
extern void AsmUpdatePlayerShots(void);
extern void AsmUpdateEnemyShots(void);
extern void AsmUpdateEnemies(void);
extern void AsmUpdateExplosions(void);
extern void AsmEnemyFire(void);

// Collision routines (68k ASM, see entities.s)
extern void AsmCollideShotsEnemies(void);
extern void AsmCollideEnemiesShip(void);
extern void AsmCollideEnemyShotsShip(void);
