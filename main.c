#include "support/gcc8_c_support.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <exec/execbase.h>
#include <graphics/gfxmacros.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>

#include "nau_dx.h"
#include "gfx.h"
#include "gfx_ship_anim.h"
#include "gfx_ship_white.h"

// ============================================================================
// SYSTEM
// ============================================================================

struct ExecBase    *SysBase;
volatile struct Custom *custom;
struct DosLibrary  *DOSBase;
struct GfxBase     *GfxBase;

static UWORD SystemInts;
static UWORD SystemDMA;
static UWORD SystemADKCON;
static volatile APTR VBR = 0;
static APTR SystemIrq;
struct View *ActiView;

static __attribute__((interrupt)) void SupervisorGetVBR() {
    __asm__ volatile(".short 0x4e7a, 0x0801");
}

static APTR GetVBR(void) {
    APTR vbr = 0;
    if (SysBase->AttnFlags & AFF_68010)
        vbr = (APTR)Supervisor((ULONG (*)())SupervisorGetVBR);
    return vbr;
}

void SetInterruptHandler(APTR interrupt) {
    *(volatile APTR*)(((UBYTE*)VBR) + 0x6c) = interrupt;
}

APTR GetInterruptHandler() {
    return *(volatile APTR*)(((UBYTE*)VBR) + 0x6c);
}

void WaitVbl() {
    debug_start_idle();
    while (1) { volatile ULONG v = *(volatile ULONG*)0xDFF004; v &= 0x1ff00; if (v != (311<<8)) break; }
    while (1) { volatile ULONG v = *(volatile ULONG*)0xDFF004; v &= 0x1ff00; if (v == (311<<8)) break; }
    debug_stop_idle();
}

void WaitLine(USHORT line) {
    while (1) {
        volatile ULONG v = *(volatile ULONG*)0xDFF004;
        if (((v >> 8) & 511) == line) break;
    }
}

__attribute__((always_inline)) inline void WaitBlt() {
    UWORD t = *(volatile UWORD*)&custom->dmaconr; (void)t;
    while (*(volatile UWORD*)&custom->dmaconr & (1<<14)) {}
}

void TakeSystem() {
    Forbid();
    SystemADKCON = custom->adkconr;
    SystemInts   = custom->intenar;
    SystemDMA    = custom->dmaconr;
    ActiView     = GfxBase->ActiView;
    LoadView(0);
    WaitTOF(); WaitTOF();
    WaitVbl(); WaitVbl();
    OwnBlitter(); WaitBlit(); Disable();
    custom->intena = 0x7fff;
    custom->intreq = 0x7fff;
    custom->dmacon = 0x7fff;
    for (int a = 0; a < 32; a++) custom->color[a] = 0;
    WaitVbl(); WaitVbl();
    VBR = GetVBR();
    SystemIrq = GetInterruptHandler();
}

void FreeSystem() {
    WaitVbl(); WaitBlit();
    custom->intena = 0x7fff;
    custom->intreq = 0x7fff;
    custom->dmacon = 0x7fff;
    SetInterruptHandler(SystemIrq);
    custom->cop1lc = (ULONG)GfxBase->copinit;
    custom->cop2lc = (ULONG)GfxBase->LOFlist;
    custom->copjmp1 = 0x7fff;
    custom->intena = SystemInts | 0x8000;
    custom->dmacon = SystemDMA  | 0x8000;
    custom->adkcon = SystemADKCON | 0x8000;
    WaitBlit(); DisownBlitter(); Enable();
    LoadView(ActiView);
    WaitTOF(); WaitTOF();
    Permit();
}

__attribute__((always_inline)) inline short MouseLeft()  { return !((*(volatile UBYTE*)0xbfe001) & 64); }
__attribute__((always_inline)) inline short MouseRight() { return !((*(volatile UWORD*)0xdff016) & (1<<10)); }

// ============================================================================
// BLITTER BOB DRAWING
// ============================================================================

#define ROW_BYTES   (SCREEN_W / 8)
#define PLANE_BYTES (ROW_BYTES * SCREEN_H)

// ============================================================================
// PARALLAX SCROLL
// ============================================================================

#define PAR_TILE_H   64
#define PAR_SPEED_0  1
#define PAR_SPEED_1  2
#define PAR_SPEED_2  3
#define PAR_SPEED_3  4

static UWORD g_TileSolid[PAR_TILE_H];  // dense outer boundary
static UWORD g_TileDeco [PAR_TILE_H];  // sparser inner wall
static short g_ParScroll[4];           // scroll offset per wall

static void ParallaxInit(void) {
    ULONG rng = 0xDEADBEEF;
    for (int i = 0; i < PAR_TILE_H; i++) {
        rng = rng * 1664525 + 1013904223;
        UWORD r1 = (UWORD)(rng >> 16);
        rng = rng * 1664525 + 1013904223;
        UWORD r2 = (UWORD)(rng >> 16);
        rng = rng * 1664525 + 1013904223;
        UWORD r3 = (UWORD)(rng >> 16);
        g_TileSolid[i] = r1 & r2;                  // ~25% lit
        g_TileDeco[i]  = r1 & r2 & r3;             // ~12% lit
    }
}

// Draw 8-pixel column at byte offset `xb` within a word
// xb: byte position (0-39), half: 0=low byte [7:0], 1=high byte [15:8]
static void DrawParByte(UBYTE* screen_mem, short xb,
                        const UWORD* tile, short scroll, int half) {
    short  xword = xb >> 1;
    UWORD  wmask = half ? 0xFF00 : 0x00FF;
    UBYTE  shift = (UBYTE)(half ? 8 : 0);
    for (short row = 0; row < SCREEN_H; row++) {
        short ti = (short)((row - scroll + PAR_TILE_H * 4) & (PAR_TILE_H - 1));
        UBYTE  m8 = (UBYTE)((tile[ti] >> shift) & 0xFF);
        // Expand 8-bit column to 16-bit word half
        UWORD  hi  = (UWORD)(((UWORD)m8 * 0x0101) & wmask);
        for (int p = 0; p < SCREEN_BPL; p++) {
            UWORD* pw = (UWORD*)(screen_mem + p * PLANE_BYTES)
                      + row * (ROW_BYTES / 2) + xword;
            UWORD c28 = (28 & (1 << p)) ? wmask : 0;
            UWORD c29 = (29 & (1 << p)) ? wmask : 0;
            *pw = (*pw & ~wmask) | (UWORD)((c28 & ~hi) | (c29 & hi));
        }
    }
}

static void ParallaxDraw(UBYTE* screen_mem) {
    // Byte layout within 16-bit word (big-endian):
    //   even byte index (0,2,4..) = high byte (bits 15-8) -> half=1
    //   odd  byte index (1,3,5..) = low  byte (bits  7-0) -> half=0
    //
    // Speeds: outer(fastest) -> inner(slowest), stepped 4->3->2->1
    // Left  (bytes 0..3):  byte0 solid@4  byte1 deco@3  byte2 deco@2  byte3 deco@1
    // Right (bytes 32..35): mirror

    // Left side
    DrawParByte(screen_mem,  0, g_TileSolid, g_ParScroll[3], 1); // word0 high, solid, speed 4
    DrawParByte(screen_mem,  1, g_TileDeco,  g_ParScroll[2], 0); // word0 low,  deco,  speed 3
    DrawParByte(screen_mem,  2, g_TileDeco,  g_ParScroll[1], 1); // word1 high, deco,  speed 2
    DrawParByte(screen_mem,  3, g_TileDeco,  g_ParScroll[0], 0); // word1 low,  deco,  speed 1

    // Right side
    DrawParByte(screen_mem, 32, g_TileDeco,  g_ParScroll[0], 1); // word16 high, deco,  speed 1
    DrawParByte(screen_mem, 33, g_TileDeco,  g_ParScroll[1], 0); // word16 low,  deco,  speed 2
    DrawParByte(screen_mem, 34, g_TileDeco,  g_ParScroll[2], 1); // word17 high, deco,  speed 3
    DrawParByte(screen_mem, 35, g_TileSolid, g_ParScroll[3], 0); // word17 low,  solid, speed 4
}

static void ParallaxUpdate(void) {
    static const short speeds[4] = { PAR_SPEED_0, PAR_SPEED_1, PAR_SPEED_2, PAR_SPEED_3 };
    for (int i = 0; i < 4; i++) {
        g_ParScroll[i] += speeds[i];
        if (g_ParScroll[i] >= PAR_TILE_H) g_ParScroll[i] -= PAR_TILE_H;
    }
}

static void ClearGameArea(UBYTE* screen_mem) {
    // Clear game area (xword 2..15), skip wall bytes 0-3 left, 32-35 right
    for (int p = 0; p < SCREEN_BPL; p++) {
        UWORD* pl = (UWORD*)(screen_mem + p * PLANE_BYTES);
        for (int row = 0; row < SCREEN_H; row++) {
            UWORD* r = pl + row * (ROW_BYTES / 2);
            for (int w = 2; w <= 15; w++) r[w] = 0;
        }
    }
}

static void DrawBob16(UBYTE* screen_mem,
                      const UWORD* mask, const UWORD* data,
                      short x, short y, UBYTE colorMask, UWORD rows) {
    if (x <= -16 || x >= SCREEN_W || y <= -(short)rows || y >= SCREEN_H) return;
    UWORD shift = (UWORD)(x & 15);
    const UWORD* m = mask;
    const UWORD* d = data;
    if (y < 0) { UWORD skip = (UWORD)(-y); m += skip; d += skip; rows = (rows > skip) ? rows - skip : 0; y = 0; }
    if (rows == 0) return;
    UWORD wx = (UWORD)(x < 0 ? 0 : x) >> 4;
    for (UWORD row = 0; row < rows; row++) {
        UWORD mv = m[row];
        UWORD dv = d[row];
        UWORD mv0 = mv >> shift;
        UWORD mv1 = shift ? (UWORD)(mv << (16 - shift)) : 0;
        UWORD dv0 = dv >> shift;
        UWORD dv1 = shift ? (UWORD)(dv << (16 - shift)) : 0;
        UWORD ry   = (UWORD)y + row;
        UWORD base = ry * (ROW_BYTES / 2) + wx;
        for (int p = 0; p < SCREEN_BPL; p++) {
            UWORD* plane = (UWORD*)(screen_mem + p * PLANE_BYTES);
            if (colorMask & (1 << p)) {
                plane[base]   = (UWORD)((plane[base]   & ~mv0) | (dv0 & mv0));
                if (wx + 1 < ROW_BYTES / 2)
                    plane[base+1] = (UWORD)((plane[base+1] & ~mv1) | (dv1 & mv1));
            } else {
                plane[base]   = (UWORD)(plane[base]   & ~mv0);
                if (wx + 1 < ROW_BYTES / 2)
                    plane[base+1] = (UWORD)(plane[base+1] & ~mv1);
            }
        }
    }
}

static void DrawBob32(UBYTE* screen_mem,
                      const UWORD* mask, const UWORD* data,
                      short x, short y, UBYTE colorMask) {
    if (x <= -32 || x >= SCREEN_W || y <= -24 || y >= SCREEN_H) return;
    UWORD shift = (UWORD)(x & 15);
    UWORD rows  = 24;
    const UWORD* m = mask;
    const UWORD* d = data;
    if (y < 0) { m += (UWORD)((-y)*2); d += (UWORD)((-y)*2); rows = (UWORD)(24+y); y = 0; }
    if (rows == 0) return;
    UWORD wx = (UWORD)(x < 0 ? 0 : x) >> 4;
    for (UWORD row = 0; row < rows; row++) {
        UWORD m0 = m[row*2],   m1 = m[row*2+1];
        UWORD d0 = d[row*2],   d1 = d[row*2+1];
        UWORD mv0 = m0 >> shift;
        UWORD mv1 = shift ? (UWORD)((m0 << (16-shift)) | (m1 >> shift)) : m1;
        UWORD mv2 = shift ? (UWORD)(m1 << (16-shift)) : 0;
        UWORD dv0 = d0 >> shift;
        UWORD dv1 = shift ? (UWORD)((d0 << (16-shift)) | (d1 >> shift)) : d1;
        UWORD dv2 = shift ? (UWORD)(d1 << (16-shift)) : 0;
        UWORD ry   = (UWORD)y + row;
        UWORD base = ry * (ROW_BYTES / 2) + wx;
        for (int p = 0; p < SCREEN_BPL; p++) {
            UWORD* plane = (UWORD*)(screen_mem + p * PLANE_BYTES);
            if (colorMask & (1 << p)) {
                plane[base]   = (UWORD)((plane[base]   & ~mv0) | (dv0 & mv0));
                plane[base+1] = (UWORD)((plane[base+1] & ~mv1) | (dv1 & mv1));
                if (wx + 2 < ROW_BYTES / 2)
                    plane[base+2] = (UWORD)((plane[base+2] & ~mv2) | (dv2 & mv2));
            } else {
                plane[base]   &= ~mv0;
                plane[base+1] &= ~mv1;
                if (wx + 2 < ROW_BYTES / 2)
                    plane[base+2] &= ~mv2;
            }
        }
    }
}

static void DrawPixel(UBYTE* screen_mem, short x, short y, UBYTE colorIdx) {
    if ((UWORD)x >= SCREEN_W || (UWORD)y >= SCREEN_H) return;
    UWORD off = (UWORD)y * ROW_BYTES + ((UWORD)x >> 3);
    UBYTE bit = (UBYTE)(0x80 >> ((UWORD)x & 7));
    for (int p = 0; p < SCREEN_BPL; p++) {
        if (colorIdx & (1<<p)) screen_mem[p * PLANE_BYTES + off] |=  bit;
        else                   screen_mem[p * PLANE_BYTES + off] &= ~bit;
    }
}

// Draw white ship (32x24) - 4 bitplanes, greyscale
static void DrawShipAnim(UBYTE* screen_mem, short x, short y, UBYTE frame) {
    (void)frame;
    if (x <= -32 || x >= SCREEN_W || y <= -24 || y >= SCREEN_H) return;
    UWORD shift = (UWORD)(x & 15);
    UWORD rows  = SHIP_W_HEIGHT;
    UWORD skip  = 0;
    if (y < 0) { skip = (UWORD)(-y); rows = (UWORD)(SHIP_W_HEIGHT - skip); y = 0; }
    if (rows == 0) return;
    UWORD wx = (UWORD)(x < 0 ? 0 : x) >> 4;
    for (UWORD row = 0; row < rows; row++) {
        UWORD ri   = row + skip;
        UWORD m0   = SHIP_W_MASK[ri*2],   m1 = SHIP_W_MASK[ri*2+1];
        UWORD mv0  = m0 >> shift;
        UWORD mv1  = shift ? (UWORD)((m0 << (16-shift)) | (m1 >> shift)) : m1;
        UWORD mv2  = shift ? (UWORD)(m1 << (16-shift)) : 0;
        UWORD ry   = (UWORD)y + row;
        UWORD base = ry * (ROW_BYTES / 2) + wx;
        for (int p = 0; p < SCREEN_BPL; p++) {
            UWORD* pl = (UWORD*)(screen_mem + p * PLANE_BYTES);
            if (p < 4) {
                // Planes 0-3: write ship data
                UWORD d0  = SHIP_W_PLANES[p][ri*2], d1 = SHIP_W_PLANES[p][ri*2+1];
                UWORD dv0 = d0 >> shift;
                UWORD dv1 = shift ? (UWORD)((d0 << (16-shift)) | (d1 >> shift)) : d1;
                UWORD dv2 = shift ? (UWORD)(d1 << (16-shift)) : 0;
                pl[base]   = (UWORD)((pl[base]   & ~mv0) | (dv0 & mv0));
                pl[base+1] = (UWORD)((pl[base+1] & ~mv1) | (dv1 & mv1));
                if (wx + 2 < ROW_BYTES / 2)
                    pl[base+2] = (UWORD)((pl[base+2] & ~mv2) | (dv2 & mv2));
            } else {
                // Plane 4: clear where ship covers (ship uses 4 BPL, screen has 5)
                pl[base]   &= ~mv0;
                pl[base+1] &= ~mv1;
                if (wx + 2 < ROW_BYTES / 2)
                    pl[base+2] &= ~mv2;
            }
        }
    }
}

static void RenderFrame(UBYTE* screen_mem); // defined after globals

// ============================================================================
// COPPER LIST HELPERS
// ============================================================================

__attribute__((always_inline)) inline USHORT* copSetColor(USHORT* p, USHORT idx, USHORT col) {
    *p++ = offsetof(struct Custom, color) + sizeof(UWORD) * idx;
    *p++ = col;
    return p;
}

__attribute__((always_inline)) inline USHORT* copWaitY(USHORT* p, USHORT y) {
    *p++ = (y << 8) | 4 | 1;
    *p++ = 0xfffe;
    return p;
}

__attribute__((always_inline)) inline USHORT* copSetReg(USHORT* p, USHORT reg, USHORT val) {
    *p++ = reg;
    *p++ = val;
    return p;
}

__attribute__((always_inline)) inline USHORT* copSetPlanes(UBYTE bplStart, USHORT* p,
                                                            const UBYTE** planes, int n) {
    for (int i = 0; i < n; i++) {
        ULONG addr = (ULONG)planes[i];
        *p++ = offsetof(struct Custom, bplpt[0]) + (i + bplStart) * sizeof(APTR);
        *p++ = (UWORD)(addr >> 16);
        *p++ = offsetof(struct Custom, bplpt[0]) + (i + bplStart) * sizeof(APTR) + 2;
        *p++ = (UWORD)addr;
    }
    return p;
}

// ============================================================================
// PALETTE - 32 colors, OCS 12-bit RGB
// Single fixed palette (no biomes)
// ============================================================================
//
// ColorMask usage:
//   0x07 = bits 0+1+2  -> boss white parts
//   0x18 = bits 3+4    -> boss red parts
//   EnemyColor: 16,17,20,22,24
//   0x1E = bits 1+2+3+4-> player/enemy shot (color 30, 0x0FF0 yellow)
//   0x1F = bits 0+1+2+3+4 -> explosion (color 31, 0x0F40 orange)
//   Stars: colorIdx 13,14,15
//   Wall parallax: colorIdx 28 (dark rock), 29 (light rock)
static const UWORD g_Palette[32] = {
    0x0000,              //  0  background black
    // Slots 1-13: nau greyscale (loaded from SHIP_W_PALETTE at runtime)
    0x0111, 0x0222, 0x0333, 0x0444, 0x0555, 0x0666, 0x0777,
    0x0888, 0x0999, 0x0AAA, 0x0BBB, 0x0CCC, 0x0DDD,
    // Slots 14-15: estrelles
    0x0555,              // 14 star dim
    0x0AAA,              // 15 star bright
    // Slots 16-27: enemics (color masks 16,17,20,22,24 + boss 0x07/0x18)
    0x0F00,              // 16 enemy red
    0x0F60,              // 17 enemy orange
    0x0FF0,              // 18 enemy yellow
    0x0AF0,              // 19 enemy green-yellow
    0x00FF,              // 20 enemy cyan
    0x008F,              // 21 enemy blue
    0x0F0F,              // 22 enemy magenta
    0x0FAA,              // 23 enemy peach
    0x0FFF,              // 24 enemy white (boss)
    0x0888,              // 25 enemy grey
    0x0F80,              // 26 enemy amber
    0x0FF8,              // 27 enemy lime
    // Slots 28-29: parallax wall rock colors
    0x0321,              // 28 wall dark rock
    0x0654,              // 29 wall light rock highlight
    // Slots 30-31: shots + explosió
    0x0FF0,              // 30 player shot yellow
    0x0F40,              // 31 explosion orange
};

// ============================================================================
// GAME STATE
// ============================================================================

static volatile short g_FrameCounter = 0;
static short g_GameState  = GS_PLAYING;
static short g_TitleMode  = TS_MENU;
static short g_CurrentBiome = 0;

// Ship
static short g_ShipX, g_ShipY;
static short g_ShipExploding  = 0;
static short g_ShipExplTimer  = 0;

// Shots
static TShot      g_Shots[MAX_SHOTS];
static short      g_FireCooldown = 0;

// Enemies
static TEnemy     g_Enemies[MAX_ENEMIES];
static TEnemyShot g_EnemyShots[MAX_ENEMY_SHOTS];
static TExplosion g_Explosions[MAX_EXPLOSIONS];

// Score / progress
static unsigned short g_Score      = 0;
static short          g_Lives      = 3;
static short          g_Level      = 1;
static unsigned short g_NextLifeAt = EXTRA_LIFE_EVERY;

// Wave state
static short g_WaveActive   = 0;
static short g_WaveTotal    = 0;
static short g_WaveSpawned  = 0;
static short g_WaveKilled   = 0;
static short g_SpawnTimer   = SPAWN_FIRST_DELAY;

// Starfield
static TStar g_Stars1[N_STARS_1];
static TStar g_Stars2[N_STARS_2];
static TStar g_Stars3[N_STARS_3];

// Hi-scores
static THiScore g_HiScores[HISCORE_COUNT];

// ============================================================================
// LEVEL TABLE (25 levels, 1:1 with MSX/CPC)
// ============================================================================

static const TLevelConfig g_Levels[ENDGAME_FINAL_LEVEL] = {
    /* 1*/  { 3, 3, LMASK_BASIC,                        0 },
    /* 2*/  { 3, 4, LMASK_BASIC|LMASK_FAST,             0 },
    /* 3*/  { 4, 3, LMASK_BASIC|LMASK_FAST,             0 },
    /* 4*/  { 4, 4, LMASK_BASIC|LMASK_FAST|LMASK_HEAVY, 0 },
    /* 5*/  { 1, 1, 0,                                  LCFG_F_BOSS1 },
    /* 6*/  { 4, 4, LMASK_BASIC|LMASK_FAST|LMASK_DIVER, 0 },
    /* 7*/  { 4, 4, LMASK_FAST|LMASK_HEAVY|LMASK_DIVER, 0 },
    /* 8*/  { 5, 4, LMASK_BASIC|LMASK_FAST|LMASK_BOMBER,0 },
    /* 9*/  { 5, 5, LMASK_FAST|LMASK_HEAVY|LMASK_DIVER|LMASK_BOMBER, 0 },
    /*10*/  { 1, 1, 0,                                  LCFG_F_BOSS1 },
    /*11*/  { 5, 4, LMASK_BASIC|LMASK_FAST|LMASK_HEAVY|LMASK_DIVER, 0 },
    /*12*/  { 5, 5, LMASK_FAST|LMASK_HEAVY|LMASK_DIVER|LMASK_BOMBER, 0 },
    /*13*/  { 6, 4, LMASK_BASIC|LMASK_FAST|LMASK_HEAVY|LMASK_BOMBER, 0 },
    /*14*/  { 6, 5, LMASK_FAST|LMASK_HEAVY|LMASK_DIVER|LMASK_BOMBER, 0 },
    /*15*/  { 1, 1, 0,                                  LCFG_F_BOSS1|LCFG_F_BOSS2 },
    /*16*/  { 6, 5, LMASK_BASIC|LMASK_FAST|LMASK_HEAVY|LMASK_DIVER|LMASK_BOMBER, 0 },
    /*17*/  { 7, 5, LMASK_FAST|LMASK_HEAVY|LMASK_DIVER|LMASK_BOMBER, 0 },
    /*18*/  { 7, 6, LMASK_BASIC|LMASK_FAST|LMASK_HEAVY|LMASK_DIVER|LMASK_BOMBER, 0 },
    /*19*/  { 7, 6, LMASK_FAST|LMASK_HEAVY|LMASK_DIVER|LMASK_BOMBER, 0 },
    /*20*/  { 1, 1, 0,                                  LCFG_F_BOSS1|LCFG_F_BOSS2 },
    /*21*/  { 8, 6, LMASK_BASIC|LMASK_FAST|LMASK_HEAVY|LMASK_DIVER|LMASK_BOMBER, 0 },
    /*22*/  { 8, 6, LMASK_FAST|LMASK_HEAVY|LMASK_DIVER|LMASK_BOMBER, 0 },
    /*23*/  { 8, 7, LMASK_BASIC|LMASK_FAST|LMASK_HEAVY|LMASK_DIVER|LMASK_BOMBER, 0 },
    /*24*/  { 8, 7, LMASK_FAST|LMASK_HEAVY|LMASK_DIVER|LMASK_BOMBER, 0 },
    /*25*/  { 1, 1, 0,                                  LCFG_F_BOSS1|LCFG_F_BOSS2 },
};

// ============================================================================
// STARFIELD
// ============================================================================

// Stars avoid wall zone: 16px padding on each side of game area
#define STAR_X0 (GAME_X0 + WALL_L_W)       // 32
#define STAR_W  (GAME_W - WALL_L_W * 2)    // 224
#define STAR_X1 (STAR_X0 + STAR_W)          // 256

static void InitStarfield() {
    // Pseudo-random init using level + index as seed
    for (int i = 0; i < N_STARS_1; i++) {
        g_Stars1[i].x = (short)(((i * 37 + 13) % STAR_W) + STAR_X0);
        g_Stars1[i].y = (short)((i * 29 + 7) % GAME_H);
    }
    for (int i = 0; i < N_STARS_2; i++) {
        g_Stars2[i].x = (short)(((i * 53 + 41) % STAR_W) + STAR_X0);
        g_Stars2[i].y = (short)((i * 17 + 19) % GAME_H);
    }
    for (int i = 0; i < N_STARS_3; i++) {
        g_Stars3[i].x = (short)(((i * 61 + 23) % STAR_W) + STAR_X0);
        g_Stars3[i].y = (short)((i * 43 + 11) % GAME_H);
    }
}

// ============================================================================
// GAME INIT
// ============================================================================

static void InitHiScores() {
    for (int i = 0; i < HISCORE_COUNT; i++) {
        g_HiScores[i].score = (HISCORE_COUNT - i) * 1000;
        g_HiScores[i].level = 1;
        g_HiScores[i].name[0] = 'A' + i;
        g_HiScores[i].name[1] = 'A';
        g_HiScores[i].name[2] = 'A';
        g_HiScores[i].name[3] = 0;
    }
}

static void ResetShip() {
    g_ShipX = SHIP_SPAWN_X;
    g_ShipY = SHIP_SPAWN_Y;
    g_ShipExploding  = 0;
    g_ShipExplTimer  = 0;
    for (int i = 0; i < MAX_SHOTS; i++) g_Shots[i].active = 0;
    g_FireCooldown = 0;
}

static void ResetGameSession() {
    g_Score      = 0;
    g_Lives      = 3;
    g_Level      = 1;
    g_NextLifeAt = EXTRA_LIFE_EVERY;
    g_CurrentBiome = 0;
    for (int i = 0; i < MAX_ENEMIES;    i++) g_Enemies[i].active    = 0;
    for (int i = 0; i < MAX_ENEMY_SHOTS;i++) g_EnemyShots[i].active = 0;
    for (int i = 0; i < MAX_EXPLOSIONS; i++) g_Explosions[i].active = 0;
    g_WaveActive  = 0;
    g_WaveSpawned = 0;
    g_WaveKilled  = 0;
    g_SpawnTimer  = SPAWN_FIRST_DELAY;
    ResetShip();
    InitStarfield();
}

// ============================================================================
// ENEMY SPAWNING
// ============================================================================

static short PickEnemyType(unsigned char mask) {
    short available[5];
    short count = 0;
    for (short t = 0; t < 5; t++) {
        if (mask & (1 << t)) available[count++] = t;
    }
    if (count == 0) return -1;
    return available[(g_FrameCounter * 7 + g_WaveSpawned * 13) % count];
}

static short FindFreeEnemy() {
    for (short i = 0; i < MAX_ENEMIES; i++) {
        if (!g_Enemies[i].active) return i;
    }
    return -1;
}

static void SpawnEnemy(short type) {
    if (type < 0) return;
    short idx = FindFreeEnemy();
    if (idx < 0) return;
    TEnemy* e = &g_Enemies[idx];
    e->active = 1;
    e->type = type;
    e->x = (short)(STAR_X0 + ((g_WaveSpawned * 37 + 13) % (STAR_W - ENEMY_W)));
    e->y = GAME_Y0 - ENEMY_H;
    e->vx = 0;
    e->fire_cd = 0;
    e->zig_timer = 0;
    e->pattern = PATT_STRAIGHT;
    switch (type) {
        case ENEMY_TYPE_BASIC:  e->health = 1; e->vy = ENEMY_SPEED_BASIC;  break;
        case ENEMY_TYPE_FAST:   e->health = 1; e->vy = ENEMY_SPEED_FAST;   break;
        case ENEMY_TYPE_HEAVY:  e->health = 3; e->vy = ENEMY_SPEED_HEAVY;  break;
        case ENEMY_TYPE_DIVER:  e->health = 2; e->vy = ENEMY_SPEED_DIVER;  break;
        case ENEMY_TYPE_BOMBER: e->health = 2; e->vy = ENEMY_SPEED_BOMBER; break;
    }
}

static void SpawnBoss(unsigned char flags) {
    (void)flags;
    short idx = FindFreeEnemy();
    if (idx < 0) return;
    TEnemy* e = &g_Enemies[idx];
    e->active = 1;
    e->type = ENEMY_TYPE_BOSS;
    e->x = (short)(STAR_X0 + STAR_W / 2 - ENEMY_BOSS_W / 2);
    e->y = GAME_Y0 - ENEMY_BOSS_H;
    e->vx = 1;
    e->vy = 1;
    e->health = BOSS_HP_BASE + ((g_Level / 5) * BOSS_HP_PER_TIER);
    e->boss_hp_max = e->health;
    e->boss_vosc = 0;
    e->fire_cd = 0;
    e->zig_timer = 0;
    e->pattern = PATT_STRAIGHT;
}

static void SpawnExplosion(short x, short y, short kind) {
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!g_Explosions[i].active) {
            g_Explosions[i].active = 1;
            g_Explosions[i].x = x;
            g_Explosions[i].y = y;
            g_Explosions[i].frame = 0;
            g_Explosions[i].kind = kind;
            break;
        }
    }
}

// ============================================================================
// INPUT
// ============================================================================

#define JOY_UP    (1<<0)
#define JOY_DOWN  (1<<1)
#define JOY_LEFT  (1<<2)
#define JOY_RIGHT (1<<3)
#define JOY_FIRE  (1<<6)

static UBYTE ReadJoy() {
    UBYTE res = 0;
    UBYTE ciaa = *(volatile UBYTE*)0xBFE001;
    if (!(ciaa & (1<<7))) res |= JOY_FIRE;

    // JOY1DAT (port 2) - Official Amiga Hardware Reference Manual Table 8-3:
    // Bit 1  = "right" switch (true logic)
    // Bit 9  = "left" switch (true logic)
    // Bit 1 XOR Bit 0 = "back" switch (DOWN)
    // Bit 9 XOR Bit 8 = "forward" switch (UP)
    UWORD joy = *(volatile UWORD*)0xDFF00C;
    if (((joy >> 9) & 1) ^ ((joy >> 8) & 1)) res |= JOY_UP;      // forward
    if (((joy >> 1) & 1) ^ ((joy >> 0) & 1)) res |= JOY_DOWN;    // back
    if ((joy >> 9) & 1) res |= JOY_LEFT;                         // left
    if ((joy >> 1) & 1) res |= JOY_RIGHT;                        // right

    return res;
}

// ============================================================================
// INTERRUPT HANDLER
// ============================================================================

static volatile USHORT* g_PendingCop = 0;

static __attribute__((interrupt)) void VBlankHandler() {
    custom->intreq = (1<<INTB_VERTB);
    custom->intreq = (1<<INTB_VERTB); // twice for A4000
    if (g_PendingCop) {
        custom->cop1lc = (ULONG)g_PendingCop;
        g_PendingCop = 0;
    }
    g_FrameCounter++;
}

// ============================================================================
// COPPER LIST BUILD
// ============================================================================


static USHORT* BuildCopperList(USHORT* cop, const UBYTE** planes) {
    const USHORT x     = 129;
    const USHORT width = 320;
    const USHORT height= 256;
    const USHORT y     = 44;
    const USHORT RES   = 8;
    USHORT xstop = x + width;
    USHORT ystop = y + height;
    USHORT fw    = (x >> 1) - RES;

    // Display window & fetch
    cop = copSetReg(cop, offsetof(struct Custom, ddfstrt),  fw);
    cop = copSetReg(cop, offsetof(struct Custom, ddfstop),  fw + (((width>>4)-1)<<3));
    cop = copSetReg(cop, offsetof(struct Custom, diwstrt),  x + (y<<8));
    cop = copSetReg(cop, offsetof(struct Custom, diwstop),  (xstop-256) + ((ystop-256)<<8));

    // 5 bitplanes, lowres
    cop = copSetReg(cop, offsetof(struct Custom, bplcon0), (1<<9) | (5<<12));
    cop = copSetReg(cop, offsetof(struct Custom, bplcon1), 0);
    cop = copSetReg(cop, offsetof(struct Custom, bplcon2), 0);
    cop = copSetReg(cop, offsetof(struct Custom, bpl1mod), 0);
    cop = copSetReg(cop, offsetof(struct Custom, bpl2mod), 0);

    // Bitplane pointers
    cop = copSetPlanes(0, cop, planes, SCREEN_BPL);

    // Load fixed palette: slots 1-13 from ship greyscale, rest from g_Palette
    cop = copSetColor(cop, 0, g_Palette[0]);
    for (int i = 1; i < 14; i++)
        cop = copSetColor(cop, i, SHIP_W_PALETTE[i]);
    for (int i = 14; i < 32; i++)
        cop = copSetColor(cop, i, g_Palette[i]);

    // End copper list
    *cop++ = 0xffff;
    *cop++ = 0xfffe;
    return cop;
}

// ============================================================================
// MAIN
// ============================================================================

static void RenderFrame(UBYTE* screen_mem) {
    ClearGameArea(screen_mem);
    ParallaxDraw(screen_mem);
    for (int i = 0; i < N_STARS_1; i++) DrawPixel(screen_mem, g_Stars1[i].x, g_Stars1[i].y, 13);
    for (int i = 0; i < N_STARS_2; i++) DrawPixel(screen_mem, g_Stars2[i].x, g_Stars2[i].y, 14);
    for (int i = 0; i < N_STARS_3; i++) DrawPixel(screen_mem, g_Stars3[i].x, g_Stars3[i].y, 15);
    if (g_GameState == GS_PLAYING || g_GameState == GS_GAMEOVER) {
        if (!g_ShipExploding) {
            UBYTE animFrame = (UBYTE)((g_FrameCounter >> 2) & 3);
            DrawShipAnim(screen_mem, g_ShipX, g_ShipY, animFrame);
        }
        for (int i = 0; i < MAX_ENEMIES; i++) {
            TEnemy* e = &g_Enemies[i];
            if (!e->active) continue;
            if (e->type == ENEMY_TYPE_BOSS) {
                DrawBob32(screen_mem, g_BossMask, g_BossWhiteData, e->x, e->y, 0x07);
                DrawBob32(screen_mem, g_BossMask, g_BossRedData,   e->x, e->y, 0x18);
            } else {
                DrawBob16(screen_mem, g_EnemyMasks[e->type], g_EnemyDatas[e->type],
                          e->x, e->y, g_EnemyColor[e->type], g_EnemyRows[e->type]);
            }
        }
        for (int i = 0; i < MAX_SHOTS; i++) {
            if (!g_Shots[i].active) continue;
            DrawBob16(screen_mem, g_ShotMask, g_ShotData, g_Shots[i].x, g_Shots[i].y, 0x1E, 8);
        }
        for (int i = 0; i < MAX_ENEMY_SHOTS; i++) {
            if (!g_EnemyShots[i].active) continue;
            DrawBob16(screen_mem, g_EShotMask, g_EShotData,
                      g_EnemyShots[i].x, g_EnemyShots[i].y, 0x1E, 8);
        }
        for (int i = 0; i < MAX_EXPLOSIONS; i++) {
            TExplosion* ex = &g_Explosions[i];
            if (!ex->active) continue;
            UWORD fr = (UWORD)((ex->frame >> 1) & 3);
            DrawBob16(screen_mem, g_ExpMasks[fr], g_ExpData[fr], ex->x, ex->y, 0x1F, 8);
        }
    }
}

int main() {
    SysBase = *((struct ExecBase**)4UL);
    custom  = (struct Custom*)0xdff000;

    GfxBase = (struct GfxBase*)OpenLibrary((CONST_STRPTR)"graphics.library", 0);
    if (!GfxBase) Exit(0);

    DOSBase = (struct DosLibrary*)OpenLibrary((CONST_STRPTR)"dos.library", 0);
    if (!DOSBase) Exit(0);

    KPrintF("Nau DX Amiga starting...\n");

    InitHiScores();
    ResetGameSession();
    ParallaxInit();

    // --- Allocate screen memory: double buffer (5 bitplanes × 2) ---
    const ULONG plane_size = (SCREEN_W / 8) * SCREEN_H; // 320/8 * 256 = 10240 bytes
    const ULONG buf_size   = plane_size * SCREEN_BPL;    // 51200 bytes per buffer
    UBYTE* screen_mem = (UBYTE*)AllocMem(buf_size * 2, MEMF_CHIP | MEMF_CLEAR);
    if (!screen_mem) { CloseLibrary((struct Library*)DOSBase); CloseLibrary((struct Library*)GfxBase); Exit(0); }
    UBYTE* draw_buf = screen_mem;
    UBYTE* show_buf = screen_mem + buf_size;

    // --- Allocate double-buffered copper lists ---
    USHORT* copper1 = (USHORT*)AllocMem(1024, MEMF_CHIP | MEMF_CLEAR);
    USHORT* copper2 = (USHORT*)AllocMem(1024, MEMF_CHIP | MEMF_CLEAR);
    if (!copper1 || !copper2) { FreeMem(screen_mem, buf_size * 2); CloseLibrary((struct Library*)DOSBase); CloseLibrary((struct Library*)GfxBase); Exit(0); }
    USHORT* cop_show  = copper1;  // currently displayed by Copper
    USHORT* cop_build = copper2;  // being built by CPU

    TakeSystem();
    WaitVbl();

    // Build initial copper list pointing to show_buf
    {
        const UBYTE* planes[SCREEN_BPL];
        for (int p = 0; p < SCREEN_BPL; p++)
            planes[p] = show_buf + p * plane_size;
        BuildCopperList(cop_show, planes);
    }
    custom->cop1lc = (ULONG)cop_show;
    custom->dmacon = DMAF_BLITTER;
    custom->copjmp1 = 0x7fff;
    custom->dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER | DMAF_BLITTER;

    // Install VBlank interrupt
    SetInterruptHandler((APTR)VBlankHandler);
    custom->intena = INTF_SETCLR | INTF_INTEN | INTF_VERTB;
    custom->intreq = (1<<INTB_VERTB);

    KPrintF("Nau DX Amiga running. Press LMB to quit.\n");

    short prev_frame = g_FrameCounter;

    // ========================================================================
    // MAIN GAME LOOP
    // ========================================================================
    while (!MouseLeft()) {
        // Wait for VBlank - at this point VBlankHandler has already applied
        // the pending copper list from last iteration
        while (g_FrameCounter == prev_frame) {}
        prev_frame = g_FrameCounter;

        // Swap screen buffers: draw_buf was just rendered last frame,
        // show_buf is what copper was showing. Now show_buf gets the new frame.
        { UBYTE* tmp = draw_buf; draw_buf = show_buf; show_buf = tmp; }

        // Build copper pointing to show_buf (the frame we just rendered)
        // into the INACTIVE copper buffer
        {
            const UBYTE* planes[SCREEN_BPL];
            for (int p = 0; p < SCREEN_BPL; p++)
                planes[p] = show_buf + p * plane_size;
            BuildCopperList(cop_build, planes);
        }
        // Schedule copper swap at next VBlank
        { USHORT* tmp = cop_build; cop_build = cop_show; cop_show = tmp; }
        g_PendingCop = cop_show;

        // Render next frame into draw_buf (copper is NOT showing this)
        RenderFrame(draw_buf);

        // --- Game logic ---
        UBYTE joy = ReadJoy();

        // --- Advance star positions (for next frame) ---
        ParallaxUpdate();
        for (int i = 0; i < N_STARS_1; i++) {
            if (++g_Stars1[i].y >= GAME_H) {
                g_Stars1[i].y = 0;
                g_Stars1[i].x = (short)(((g_Stars1[i].x * 37 + 13) % STAR_W) + STAR_X0);
            }
        }
        for (int i = 0; i < N_STARS_2; i++) {
            g_Stars2[i].y += (g_FrameCounter & 1) ? 2 : 1;
            if (g_Stars2[i].y >= GAME_H) {
                g_Stars2[i].y = 0;
                g_Stars2[i].x = (short)(((g_Stars2[i].x * 53 + 41) % STAR_W) + STAR_X0);
            }
        }
        for (int i = 0; i < N_STARS_3; i++) {
            g_Stars3[i].y += 3;
            if (g_Stars3[i].y >= GAME_H) {
                g_Stars3[i].y = 0;
                g_Stars3[i].x = (short)(((g_Stars3[i].x * 61 + 23) % STAR_W) + STAR_X0);
            }
        }

        // --- Game state machine ---
        if (g_GameState == GS_TITLE) {
            // Title screen: just animate stars, wait for fire
            if (joy & JOY_FIRE) {
                g_GameState = GS_PLAYING;
                ResetGameSession();
            }

        } else if (g_GameState == GS_PLAYING) {
            // --- Move ship ---
            if (!g_ShipExploding) {
                if ((joy & JOY_LEFT)  && g_ShipX > SHIP_MIN_X) g_ShipX -= SHIP_SPEED_X;
                if ((joy & JOY_RIGHT) && g_ShipX < SHIP_MAX_X) g_ShipX += SHIP_SPEED_X;
                if ((joy & JOY_UP)    && g_ShipY > SHIP_MIN_Y) g_ShipY -= SHIP_SPEED_Y;
                if ((joy & JOY_DOWN)  && g_ShipY < SHIP_MAX_Y) g_ShipY += SHIP_SPEED_Y;

                // Fire
                if ((joy & JOY_FIRE) && g_FireCooldown == 0) {
                    for (int i = 0; i < MAX_SHOTS; i++) {
                        if (!g_Shots[i].active) {
                            g_Shots[i].active = 1;
                            g_Shots[i].x = (short)(g_ShipX + SHIP_W/2 - SHOT_W/2);
                            g_Shots[i].y = (short)(g_ShipY - SHOT_H);
                            g_FireCooldown = FIRE_COOLDOWN;
                            break;
                        }
                    }
                }
                if (g_FireCooldown > 0) g_FireCooldown--;

            } else {
                // Exploding
                g_ShipExplTimer--;
                if (g_ShipExplTimer <= 0) {
                    g_ShipExploding = 0;
                    if (g_Lives > 0) {
                        ResetShip();
                    } else {
                        g_GameState = GS_GAMEOVER;
                    }
                }
            }

            // --- Wave spawning ---
            if (!g_WaveActive) {
                if (g_Level <= ENDGAME_FINAL_LEVEL) {
                    const TLevelConfig* cfg = &g_Levels[g_Level - 1];
                    g_WaveTotal   = (cfg->mask != 0) ? cfg->waves * cfg->per_wave : 0;
                    g_WaveSpawned = 0;
                    g_WaveKilled  = 0;
                    g_SpawnTimer  = SPAWN_FIRST_DELAY;
                    g_WaveActive  = 1;
                    if (cfg->flags & LCFG_F_BOSS1) SpawnBoss(cfg->flags);
                    if (cfg->flags & LCFG_F_BOSS2) SpawnBoss(cfg->flags);
                }
            } else {
                const TLevelConfig* cfg = &g_Levels[g_Level - 1];
                if (g_WaveSpawned < g_WaveTotal) {
                    g_SpawnTimer--;
                    if (g_SpawnTimer <= 0) {
                        short type = PickEnemyType(cfg->mask);
                        SpawnEnemy(type);
                        g_WaveSpawned++;
                        g_SpawnTimer = SERIAL_DELAY;
                    }
                } else {
                    short allDead = 1;
                    for (int i = 0; i < MAX_ENEMIES; i++) {
                        if (g_Enemies[i].active) { allDead = 0; break; }
                    }
                    if (allDead) {
                        g_WaveActive = 0;
                        g_Level++;
                        g_CurrentBiome = ((g_Level - 1) / 5) % BIOME_COUNT;
                    }
                }
            }

            // --- Update player shots ---
            for (int i = 0; i < MAX_SHOTS; i++) {
                if (!g_Shots[i].active) continue;
                g_Shots[i].y -= SHOT_SPEED;
                if (g_Shots[i].y < GAME_Y0) g_Shots[i].active = 0;
            }

            // --- Update enemies ---
            for (int i = 0; i < MAX_ENEMIES; i++) {
                TEnemy* e = &g_Enemies[i];
                if (!e->active) continue;
                if (e->type == ENEMY_TYPE_BOSS) {
                    if (e->y < BOSS_HOLD_Y) {
                        e->y += e->vy;
                    } else {
                        e->boss_vosc++;
                        e->y = BOSS_HOLD_Y + (e->boss_vosc & 8 ? 4 : -4);
                        if (e->boss_vosc & 4) {
                            e->x += 1;
                            if (e->x >= STAR_X1 - ENEMY_BOSS_W - 4) e->x = (short)(STAR_X1 - ENEMY_BOSS_W - 4);
                        } else {
                            e->x -= 1;
                            if (e->x <= STAR_X0 + 4) e->x = (short)(STAR_X0 + 4);
                        }
                    }
                } else {
                    e->y += e->vy;
                    if (e->y > GAME_H) { e->active = 0; g_WaveKilled++; }
                }

                // Simple shot-enemy collision
                for (int s = 0; s < MAX_SHOTS; s++) {
                    if (!g_Shots[s].active) continue;
                    UWORD ew = (e->type == ENEMY_TYPE_BOSS) ? ENEMY_BOSS_W : ENEMY_W;
                    UWORD eh = (e->type == ENEMY_TYPE_BOSS) ? ENEMY_BOSS_H : ENEMY_H;
                    if (g_Shots[s].x + SHOT_W  > e->x &&
                        g_Shots[s].x            < e->x + ew &&
                        g_Shots[s].y + SHOT_H  > e->y &&
                        g_Shots[s].y            < e->y + eh) {
                        g_Shots[s].active = 0;
                        e->health--;
                        if (e->health <= 0) {
                            e->active = 0;
                            g_WaveKilled++;
                            switch (e->type) {
                                case ENEMY_TYPE_BASIC:  g_Score += ENEMY_SCORE_BASIC;  break;
                                case ENEMY_TYPE_FAST:   g_Score += ENEMY_SCORE_FAST;   break;
                                case ENEMY_TYPE_HEAVY:  g_Score += ENEMY_SCORE_HEAVY;  break;
                                case ENEMY_TYPE_DIVER:  g_Score += ENEMY_SCORE_DIVER;  break;
                                case ENEMY_TYPE_BOMBER: g_Score += ENEMY_SCORE_BOMBER; break;
                                case ENEMY_TYPE_BOSS:   g_Score += ENEMY_SCORE_BOSS;   break;
                            }
                            SpawnExplosion(e->x, e->y, e->type == ENEMY_TYPE_BOSS ? EXP_KIND_BOSS : EXP_KIND_ENEMY);
                            // Check extra life
                            if (g_Score >= g_NextLifeAt) {
                                g_Lives++;
                                g_NextLifeAt += EXTRA_LIFE_EVERY;
                            }
                        }
                    }
                }

                // Enemy-ship collision
                if (!g_ShipExploding) {
                    UWORD ew = (e->type == ENEMY_TYPE_BOSS) ? ENEMY_BOSS_W : ENEMY_W;
                    UWORD eh = (e->type == ENEMY_TYPE_BOSS) ? ENEMY_BOSS_H : ENEMY_H;
                    if (e->x + ew  > g_ShipX + SHIP_HIT_OX &&
                        e->x       < g_ShipX + SHIP_HIT_OX + SHIP_HIT_W &&
                        e->y + eh  > g_ShipY + SHIP_HIT_OY &&
                        e->y       < g_ShipY + SHIP_HIT_OY + SHIP_HIT_H) {
                        e->active = 0;
                        g_WaveKilled++;
                        SpawnExplosion(e->x, e->y, EXP_KIND_ENEMY);
                        SpawnExplosion(g_ShipX, g_ShipY, EXP_KIND_SHIP);
                        g_ShipExploding  = 1;
                        g_ShipExplTimer  = SHIP_EXPL_TIMER;
                        g_Lives--;
                    }
                }
            }

            // --- Update explosions ---
            for (int i = 0; i < MAX_EXPLOSIONS; i++) {
                if (!g_Explosions[i].active) continue;
                g_Explosions[i].frame++;
                if (g_Explosions[i].frame >= EXP_FRAMES * 4)
                    g_Explosions[i].active = 0;
            }

            // Check game over
            if (g_GameState == GS_PLAYING && g_Level > ENDGAME_FINAL_LEVEL)
                g_GameState = GS_WIN;

        } else if (g_GameState == GS_GAMEOVER || g_GameState == GS_WIN) {
            if (joy & JOY_FIRE) {
                g_GameState = GS_TITLE;
                g_TitleMode = TS_MENU;
                ResetGameSession();
            }
        }

    }

    // ========================================================================
    // SHUTDOWN
    // ========================================================================
    FreeSystem();

    FreeMem(screen_mem, buf_size * 2);
    FreeMem(copper1, 1024);
    FreeMem(copper2, 1024);

    CloseLibrary((struct Library*)DOSBase);
    CloseLibrary((struct Library*)GfxBase);
}
