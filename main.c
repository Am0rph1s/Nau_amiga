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

static void ClearGameArea(UBYTE* screen_mem) {
    ULONG* p = (ULONG*)screen_mem;
    ULONG  n = (PLANE_BYTES * SCREEN_BPL) / 4;
    while (n--) *p++ = 0;
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
    UWORD wx = (UWORD)(x < 0 ? 0 : x) >> 4; // word offset in row
    for (UWORD row = 0; row < rows; row++) {
        UWORD mv = m[row];
        UWORD dv = d[row];
        UWORD mv0 = mv >> shift;
        UWORD mv1 = shift ? (UWORD)(mv << (16 - shift)) : 0;
        UWORD dv0 = dv >> shift;
        UWORD dv1 = shift ? (UWORD)(dv << (16 - shift)) : 0;
        UWORD ry   = (UWORD)y + row;
        UWORD base = ry * (ROW_BYTES / 2) + wx; // word index into plane
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

// Draw animated ship (32x24) - 5 bitplanes, full colour from Nau.png palette
static void DrawShipAnim(UBYTE* screen_mem, short x, short y, UBYTE frame) {
    (void)frame; // reserved for engine animation
    if (x <= -32 || x >= SCREEN_W || y <= -24 || y >= SCREEN_H) return;
    UWORD shift = (UWORD)(x & 15);
    UWORD rows  = SHIP_ANIM_HEIGHT;
    const UWORD* mask = SHIP_MASK;
    if (y < 0) {
        UWORD skip = (UWORD)(-y);
        mask += skip * 2;
        rows  = (rows > skip) ? rows - skip : 0;
        y = 0;
    }
    if (rows == 0) return;
    UWORD wx = (UWORD)(x < 0 ? 0 : x) >> 4;
    for (UWORD row = 0; row < rows; row++) {
        UWORD m0 = mask[row*2], m1 = mask[row*2+1];
        UWORD mv0 = m0 >> shift;
        UWORD mv1 = shift ? (UWORD)((m0 << (16-shift)) | (m1 >> shift)) : m1;
        UWORD mv2 = shift ? (UWORD)(m1 << (16-shift)) : 0;
        UWORD ry   = (UWORD)y + row;
        UWORD base = ry * (ROW_BYTES / 2) + wx;
        for (int p = 0; p < 5; p++) {
            const UWORD* pd = SHIP_PLANES[p] + (mask - SHIP_MASK);
            UWORD d0 = pd[row*2], d1 = pd[row*2+1];
            UWORD dv0 = d0 >> shift;
            UWORD dv1 = shift ? (UWORD)((d0 << (16-shift)) | (d1 >> shift)) : d1;
            UWORD dv2 = shift ? (UWORD)(d1 << (16-shift)) : 0;
            UWORD* pl = (UWORD*)(screen_mem + p * PLANE_BYTES);
            pl[base]   = (UWORD)((pl[base]   & ~mv0) | (dv0 & mv0));
            pl[base+1] = (UWORD)((pl[base+1] & ~mv1) | (dv1 & mv1));
            if (wx + 2 < ROW_BYTES / 2)
                pl[base+2] = (UWORD)((pl[base+2] & ~mv2) | (dv2 & mv2));
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
// Biome palettes: each biome changes sky + wall colors
// ============================================================================

// Biome 0: Rocky  - greys/whites
// Biome 1: Ice    - blues/cyans
// Biome 2: Forest - greens/yellows
// Biome 3: Fire   - reds/oranges
// Biome 4: Tech   - purples/teals

// Ship palette (slots 1-12, from Nau.png):
// 1=#112 2=#224 3=#346 4=#468 5=#68A  <- dark/mid/light blue-grey (hull)
// 6=#311 7=#631 8=#B51 9=#FA3         <- rust/orange/yellow (accents)
// 10=#031 11=#9AC 12=#DEF             <- dark green, light grey, near-white
static const UWORD g_Palette[5][32] = {
    // Biome 0: Rocky
    { 0x0000,                          // 0  background (black)
      0x0112, 0x0224, 0x0346,          // 1-3  ship hull dark
      0x0468, 0x068A, 0x0311, 0x0631, // 4-7  ship hull mid + rust
      0x0B51, 0x0FA3, 0x0031, 0x09AC, // 8-11 ship orange/yellow/green/grey
      0x0DEF, 0x0AAA, 0x0888, 0x0FFF, // 12-15 near-white, star greys
      0x0F00, 0x0F40, 0x0FA0, 0x0FF0, // 16-19 enemy basic
      0x0088, 0x00FF, 0x044F, 0x00AA, // 20-23 enemy heavy/diver
      0x0F80, 0x0FA0, 0x0FFF, 0x0AAA, // 24-27 enemy bomber/boss
      0x0F00, 0x0F44, 0x0FF0, 0x0888 }, // 28-31 shot/explosion
    // Biome 1: Ice
    { 0x0001,
      0x0112, 0x0224, 0x0346,
      0x0468, 0x068A, 0x0311, 0x0631,
      0x0B51, 0x0FA3, 0x0031, 0x09AC,
      0x0DEF, 0x08BF, 0x066C, 0x0FFF,
      0x00FF, 0x04FF, 0x08FF, 0x0CFF,
      0x0088, 0x00FF, 0x044F, 0x00AA,
      0x0F80, 0x0FA0, 0x0FFF, 0x0AAA,
      0x0F00, 0x0F44, 0x0FF0, 0x0888 },
    // Biome 2: Forest
    { 0x0010,
      0x0112, 0x0224, 0x0346,
      0x0468, 0x068A, 0x0311, 0x0631,
      0x0B51, 0x0FA3, 0x0031, 0x09AC,
      0x0DEF, 0x08C0, 0x0690, 0x0AF0,
      0x00FF, 0x04FF, 0x08FF, 0x0CFF,
      0x0088, 0x00FF, 0x044F, 0x00AA,
      0x0F80, 0x0FA0, 0x0FFF, 0x0AAA,
      0x0F00, 0x0F44, 0x0FF0, 0x0888 },
    // Biome 3: Fire
    { 0x0100,
      0x0112, 0x0224, 0x0346,
      0x0468, 0x068A, 0x0311, 0x0631,
      0x0B51, 0x0FA3, 0x0031, 0x09AC,
      0x0DEF, 0x0F60, 0x0F20, 0x0FA0,
      0x00FF, 0x04FF, 0x08FF, 0x0CFF,
      0x0088, 0x00FF, 0x044F, 0x00AA,
      0x0F80, 0x0FA0, 0x0FFF, 0x0AAA,
      0x0F00, 0x0F44, 0x0FF0, 0x0888 },
    // Biome 4: Tech
    { 0x0001,
      0x0112, 0x0224, 0x0346,
      0x0468, 0x068A, 0x0311, 0x0631,
      0x0B51, 0x0FA3, 0x0031, 0x09AC,
      0x0DEF, 0x068C, 0x046A, 0x08AF,
      0x00FF, 0x04FF, 0x08FF, 0x0CFF,
      0x0088, 0x00FF, 0x044F, 0x00AA,
      0x0F80, 0x0FA0, 0x0FFF, 0x0AAA,
      0x0F00, 0x0F44, 0x0FF0, 0x0888 },
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

static void InitStarfield() {
    // Pseudo-random init using level + index as seed
    for (int i = 0; i < N_STARS_1; i++) {
        g_Stars1[i].x = (short)(((i * 37 + 13) % GAME_W) + GAME_X0);
        g_Stars1[i].y = (short)((i * 29 + 7) % GAME_H);
    }
    for (int i = 0; i < N_STARS_2; i++) {
        g_Stars2[i].x = (short)(((i * 53 + 41) % GAME_W) + GAME_X0);
        g_Stars2[i].y = (short)((i * 17 + 19) % GAME_H);
    }
    for (int i = 0; i < N_STARS_3; i++) {
        g_Stars3[i].x = (short)(((i * 61 + 23) % GAME_W) + GAME_X0);
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
    e->x = (short)(GAME_X0 + ((g_WaveSpawned * 37 + 13) % (GAME_W - ENEMY_W)));
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
    e->x = (short)(GAME_X0 + GAME_W / 2 - ENEMY_BOSS_W / 2);
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

static __attribute__((interrupt)) void VBlankHandler() {
    custom->intreq = (1<<INTB_VERTB);
    custom->intreq = (1<<INTB_VERTB); // twice for A4000
    g_FrameCounter++;
}

// ============================================================================
// COPPER LIST BUILD
// ============================================================================

static USHORT* BuildCopperList(USHORT* cop, const UBYTE** planes, int biome) {
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

    // Load palette
    const UWORD* pal = g_Palette[biome];
    for (int i = 0; i < 32; i++)
        cop = copSetColor(cop, i, pal[i]);

    // Raster color effects for biome flavor
    // Sky gradient: top 64 lines slightly lighter background
    cop = copWaitY(cop, 44);
    cop = copSetColor(cop, 0, pal[0]);

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
    for (int i = 0; i < N_STARS_1; i++) DrawPixel(screen_mem, g_Stars1[i].x, g_Stars1[i].y, 13);
    for (int i = 0; i < N_STARS_2; i++) DrawPixel(screen_mem, g_Stars2[i].x, g_Stars2[i].y, 14);
    for (int i = 0; i < N_STARS_3; i++) DrawPixel(screen_mem, g_Stars3[i].x, g_Stars3[i].y, 15);
    if (g_GameState == GS_PLAYING || g_GameState == GS_GAMEOVER) {
        if (!g_ShipExploding) {
            // Animate ship based on frame counter (cycles through 4 frames)
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
            DrawBob16(screen_mem, g_ShotMask, g_ShotData, g_Shots[i].x, g_Shots[i].y, 0x1C, 8);
        }
        for (int i = 0; i < MAX_ENEMY_SHOTS; i++) {
            if (!g_EnemyShots[i].active) continue;
            DrawBob16(screen_mem, g_EShotMask, g_EShotData,
                      g_EnemyShots[i].x, g_EnemyShots[i].y, 0x1D, 8);
        }
        for (int i = 0; i < MAX_EXPLOSIONS; i++) {
            TExplosion* ex = &g_Explosions[i];
            if (!ex->active) continue;
            UWORD fr = (UWORD)((ex->frame >> 1) & 3);
            DrawBob16(screen_mem, g_ExpMasks[fr], g_ExpData[fr], ex->x, ex->y, 0x1E, 8);
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

    // --- Allocate screen memory: double buffer (5 bitplanes × 2) ---
    const ULONG plane_size = (SCREEN_W / 8) * SCREEN_H; // 320/8 * 256 = 10240 bytes
    const ULONG buf_size   = plane_size * SCREEN_BPL;    // 51200 bytes per buffer
    UBYTE* screen_mem = (UBYTE*)AllocMem(buf_size * 2, MEMF_CHIP | MEMF_CLEAR);
    if (!screen_mem) { CloseLibrary((struct Library*)DOSBase); CloseLibrary((struct Library*)GfxBase); Exit(0); }
    UBYTE* draw_buf = screen_mem;
    UBYTE* show_buf = screen_mem + buf_size;

    // --- Allocate copper list ---
    USHORT* copper1 = (USHORT*)AllocMem(512, MEMF_CHIP);
    if (!copper1) { FreeMem(screen_mem, buf_size * 2); CloseLibrary((struct Library*)DOSBase); CloseLibrary((struct Library*)GfxBase); Exit(0); }

    TakeSystem();
    WaitVbl();

    // Build initial copper list pointing to show_buf
    {
        const UBYTE* planes[SCREEN_BPL];
        for (int p = 0; p < SCREEN_BPL; p++)
            planes[p] = show_buf + p * plane_size;
        BuildCopperList(copper1, planes, g_CurrentBiome);
    }

    custom->cop1lc = (ULONG)copper1;
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
        // Wait for new frame
        while (g_FrameCounter == prev_frame) {}
        prev_frame = g_FrameCounter;

        // --- Swap buffers ---
        { UBYTE* tmp = draw_buf; draw_buf = show_buf; show_buf = tmp; }

        // --- Update copper to display show_buf ---
        {
            const UBYTE* planes[SCREEN_BPL];
            for (int p = 0; p < SCREEN_BPL; p++)
                planes[p] = show_buf + p * plane_size;
            BuildCopperList(copper1, planes, g_CurrentBiome);
        }
        custom->cop1lc = (ULONG)copper1;

        // --- Render to draw_buf (off-screen) ---
        RenderFrame(draw_buf);

        // --- Game logic ---
        WaitLine(0x10);

        UBYTE joy = ReadJoy();

        // --- Advance star positions (for next frame) ---
        for (int i = 0; i < N_STARS_1; i++) {
            if (++g_Stars1[i].y >= GAME_H) {
                g_Stars1[i].y = 0;
                g_Stars1[i].x = (short)(((g_Stars1[i].x * 37 + 13) % GAME_W) + GAME_X0);
            }
        }
        for (int i = 0; i < N_STARS_2; i++) {
            g_Stars2[i].y += (g_FrameCounter & 1) ? 2 : 1;
            if (g_Stars2[i].y >= GAME_H) {
                g_Stars2[i].y = 0;
                g_Stars2[i].x = (short)(((g_Stars2[i].x * 53 + 41) % GAME_W) + GAME_X0);
            }
        }
        for (int i = 0; i < N_STARS_3; i++) {
            g_Stars3[i].y += 3;
            if (g_Stars3[i].y >= GAME_H) {
                g_Stars3[i].y = 0;
                g_Stars3[i].x = (short)(((g_Stars3[i].x * 61 + 23) % GAME_W) + GAME_X0);
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
                            if (e->x >= GAME_X1 - ENEMY_BOSS_W - 4) e->x = (short)(GAME_X1 - ENEMY_BOSS_W - 4);
                        } else {
                            e->x -= 1;
                            if (e->x <= GAME_X0 + 4) e->x = (short)(GAME_X0 + 4);
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
    FreeMem(copper1, 512);

    CloseLibrary((struct Library*)DOSBase);
    CloseLibrary((struct Library*)GfxBase);
}
