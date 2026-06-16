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
#include "gfx_ship_white.h"
#include "blitter.h"
#include "gfx_enemy_basic24.h"
#include "gfx_enemy_fast16.h"
#include "gfx_bg_tiles.h"
#include "gfx_border.h"

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
// HW SPRITE MULTIPLEXING (player shots, 4 attached pairs)
// ============================================================================
#define SPR_CHAN_WORDS 12
#define SPR_MAX_PAIRS 4
#define SPR_FIRST_USABLE_PAIR 2
#define SPR_USABLE_PAIRS 2
#define SPR_SHOT_ROWS 8
static UWORD* g_SprDataA = 0;
static UWORD* g_SprDataB = 0;
static UWORD* g_SprDataActive = 0;
static UWORD* g_SprDataBuild = 0;
static UWORD* g_SprDataPending = 0;
static short g_SprPairShotActive[SPR_MAX_PAIRS];
static short g_SprPairShotBuild[SPR_MAX_PAIRS];

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

// Draw 8-pixel column at byte offset `xb` — plane 0 only
// Planes 1-4 are static (init once at startup)
static void DrawParByteBpl0(UBYTE* screen_mem, short xb,
                            const UWORD* tile, short scroll, int half) {
    short  xword = xb >> 1;
    UWORD  wmask = half ? 0xFF00 : 0x00FF;
    UBYTE  shift = (UBYTE)(half ? 8 : 0);
    UWORD* plane0 = (UWORD*)(screen_mem + 0 * PLANE_BYTES);
    for (short row = 0; row < SCREEN_H; row++) {
        short ti = (short)((row - scroll + PAR_TILE_H * 4) & (PAR_TILE_H - 1));
        UBYTE  m8 = (UBYTE)((tile[ti] >> shift) & 0xFF);
        UWORD  hi  = (UWORD)(((UWORD)m8 * 0x0101) & wmask);
        UWORD* pw = plane0 + row * (ROW_BYTES / 2) + xword;
        // Color 28 (dark):  plane0=0 → clear byte; Color 29 (light): plane0=1 → set byte
        *pw = (*pw & ~wmask) | hi;
    }
}

// One-time init of static PF1 wall planes: 2 and 4 = 0xFF for wall bytes
// Planes 1,3 (PF2) stay 0 (MEMF_CLEAR)
static void ParallaxInitWalls(UBYTE* mem) {
    static const int pf1_hi_planes[2] = { 2, 4 };
    for (int i = 0; i < 2; i++) {
        UBYTE* plane = mem + pf1_hi_planes[i] * PLANE_BYTES;
        for (int row = 0; row < SCREEN_H; row++) {
            UBYTE* r = plane + row * ROW_BYTES;
            r[0] = r[1] = r[2] = r[3] = 0xFF;
            r[32] = r[33] = r[34] = r[35] = 0xFF;
        }
    }
}

static void ParallaxDraw(UBYTE* screen_mem) {
    // Dual-playfield: only write plane 0 (PF1 bit 0) tile pattern
    // Planes 2,4 (PF1 bits 1,2) are static 0xFF set by ParallaxInitWalls
    // Planes 1,3 (PF2) are cleared by ClearGameAreaAsm
    for (short row = 0; row < SCREEN_H; row++) {
        short t0 = (short)((row - g_ParScroll[0] + PAR_TILE_H * 4) & (PAR_TILE_H - 1));
        short t1 = (short)((row - g_ParScroll[1] + PAR_TILE_H * 4) & (PAR_TILE_H - 1));
        short t2 = (short)((row - g_ParScroll[2] + PAR_TILE_H * 4) & (PAR_TILE_H - 1));
        short t3 = (short)((row - g_ParScroll[3] + PAR_TILE_H * 4) & (PAR_TILE_H - 1));
        UBYTE* r = screen_mem + row * ROW_BYTES;
        r[0]  = (UBYTE)(g_TileSolid[t3] >> 8);
        r[1]  = (UBYTE)(g_TileDeco[t2] & 0xFF);
        r[2]  = (UBYTE)(g_TileDeco[t1] >> 8);
        r[3]  = (UBYTE)(g_TileDeco[t0] & 0xFF);
        r[32] = (UBYTE)(g_TileDeco[t0] >> 8);
        r[33] = (UBYTE)(g_TileDeco[t1] & 0xFF);
        r[34] = (UBYTE)(g_TileDeco[t2] >> 8);
        r[35] = (UBYTE)(g_TileSolid[t3] & 0xFF);
    }
}

static void ParallaxUpdate(void) {
    static const short speeds[4] = { PAR_SPEED_0, PAR_SPEED_1, PAR_SPEED_2, PAR_SPEED_3 };
    for (int i = 0; i < 4; i++) {
        g_ParScroll[i] += speeds[i];
        if (g_ParScroll[i] >= PAR_TILE_H) g_ParScroll[i] -= PAR_TILE_H;
    }
}

static void ClearGameArea(UBYTE* screen_mem) {
    ClearGameAreaAsm(screen_mem);
}

// Draw foreground border (64px, 3-bitplane) on PF2 — ASM optimized with movem
static void DrawBorder(UBYTE* screen_mem, short scroll_y) {
    DrawBorderAsm(screen_mem, border_data, border_mirror_data, (int)scroll_y);
}

// Check ship collision with border (any non-zero pixel = solid)
static short CheckBorderCollision(short ship_x, short ship_y, short scroll_y) {
    int border_h = BORDER_H;
    scroll_y = scroll_y % border_h;
    if (scroll_y < 0) scroll_y += border_h;

    int sx0 = ship_x + SHIP_HIT_OX;
    int sy0 = ship_y + SHIP_HIT_OY;
    int sx1 = sx0 + SHIP_HIT_W;
    int sy1 = sy0 + SHIP_HIT_H;

    for (int sy = sy0; sy < sy1; sy++) {
        if (sy < 0 || sy >= SCREEN_H) continue;
        int src_row = (scroll_y + sy) % border_h;
        int row_base = src_row * BORDER_ROW_BYTES;

        for (int sx = sx0; sx < sx1; sx++) {
            if (sx < 0 || sx >= SCREEN_W) continue;
            int byte_off = 0, bit = 0;
            const UBYTE* data;

            if (sx < BORDER_W) {
                byte_off = sx / 8;
                bit = 7 - (sx & 7);
                data = border_data;
            } else if (sx >= SCREEN_W - BORDER_W) {
                int rx = sx - (SCREEN_W - BORDER_W);
                byte_off = rx / 8;
                bit = 7 - (rx & 7);
                data = border_mirror_data;
            } else {
                continue;
            }

            for (int p = 0; p < 3; p++) {
                if (data[p * BORDER_PLANE_SIZE + row_base + byte_off] & (1 << bit))
                    return 1;
            }
        }
    }
    return 0;
}

// Pre-render tilemap background into bg_buf (called once at startup)
#define BG_PLANE_BYTES (ROW_BYTES * (BG_MAP_ROWS * BG_TILE_H + SCREEN_H))
static const int bg_plane_offs[BG_BPL] = { 0, 1, 2 };  // sequential planes in bg_buf

static void InitTilemapBG(UBYTE* bg_buf) {
    for (int map_row = 0; map_row < BG_MAP_ROWS; map_row++) {
        for (int tx = 0; tx < BG_MAP_COLS; tx++) {
            UWORD tile_idx = bg_tilemap[map_row * BG_MAP_COLS + tx];
            const UBYTE* tile = bg_tiles[tile_idx];
            int scr_x = tx * BG_TILE_W;

            for (int tile_row = 0; tile_row < BG_TILE_H; tile_row++) {
                int dst_y = map_row * BG_TILE_H + tile_row;
                UBYTE* dst_row = bg_buf + dst_y * ROW_BYTES;
                int byte_off = scr_x / 8;

                for (int bpl = 0; bpl < BG_BPL; bpl++) {
                    UBYTE* dst_plane = dst_row + bg_plane_offs[bpl] * BG_PLANE_BYTES;
                    int tile_off = bpl * (BG_TILE_H * BG_TILE_W / 8) + tile_row * (BG_TILE_W / 8);
                    dst_plane[byte_off]     = tile[tile_off];
                    dst_plane[byte_off + 1] = tile[tile_off + 1];
                }
            }
        }
    }
}

// Draw 16px bob on PF2 only (planes 1,3,5)
// colorMask: bit 0 -> plane 1, bit 1 -> plane 3, bit 2 -> plane 5
static void DrawBob16(UBYTE* screen_mem,
                      const UWORD* mask, const UWORD* data,
                      short x, short y, UBYTE colorMask, UWORD rows) {
    if (!DrawBob16Asm(screen_mem, mask, data, x, y, colorMask, rows))
        return;
    if (x <= -16 || x >= SCREEN_W || y <= -(short)rows || y >= SCREEN_H) return;
    UWORD shift = (UWORD)(x & 15);
    const UWORD* m = mask;
    const UWORD* d = data;
    if (y < 0) { UWORD skip = (UWORD)(-y); m += skip; d += skip; rows = (rows > skip) ? rows - skip : 0; y = 0; }
    if (y + (short)rows > SCREEN_H) rows = (UWORD)(SCREEN_H - y);
    if (rows == 0) return;
    UWORD wx = (UWORD)(x < 0 ? 0 : x) >> 4;
    static const UBYTE pf2_planes[3] = { 1, 3, 5 };
    for (UWORD row = 0; row < rows; row++) {
        UWORD mv = m[row];
        UWORD dv = d[row];
        UWORD mv0 = mv >> shift;
        UWORD mv1 = shift ? (UWORD)(mv << (16 - shift)) : 0;
        UWORD dv0 = dv >> shift;
        UWORD dv1 = shift ? (UWORD)(dv << (16 - shift)) : 0;
        UWORD ry   = (UWORD)y + row;
        UWORD base = ry * (ROW_BYTES / 2) + wx;
        for (int i = 0; i < 3; i++) {
            UWORD* plane = (UWORD*)(screen_mem + pf2_planes[i] * PLANE_BYTES);
            if (colorMask & (1 << i)) {
                plane[base]   = (UWORD)((plane[base]   & ~mv0) | (dv0 & mv0));
                if (wx + 1 < ROW_BYTES / 2)
                    plane[base+1] = (UWORD)((plane[base+1] & ~mv1) | (dv1 & mv1));
            } else {
                plane[base]   &= ~mv0;
                if (wx + 1 < ROW_BYTES / 2)
                    plane[base+1] &= ~mv1;
            }
        }
    }
}

// Draw 32px-wide bob with 2 independent data bitplanes (for multi-tone sprites)
// planeHi/planeLo: which bitplane indices carry the hi/lo data respectively
static void DrawBob32_2bpl(UBYTE* screen_mem,
                           const UWORD* mask, const UWORD* dataHi, const UWORD* dataLo,
                           short x, short y, UBYTE planeHi, UBYTE planeLo) {
    if (!DrawBob32d2Asm(screen_mem, mask, dataHi, dataLo, x, y, planeHi, planeLo))
        return;
    if (x <= -32 || x >= SCREEN_W || y <= -24 || y >= SCREEN_H) return;
    UWORD shift = (UWORD)(x & 15);
    UWORD rows  = 24;
    const UWORD* m = mask;
    const UWORD* dh = dataHi;
    const UWORD* dl = dataLo;
    if (y < 0) { UWORD skip = (UWORD)(-y)*2; m += skip; dh += skip; dl += skip; rows = (UWORD)(24+y); y = 0; }
    if (y + (short)rows > SCREEN_H) rows = (UWORD)(SCREEN_H - y);
    if (rows == 0) return;
    UWORD wx = (UWORD)(x < 0 ? 0 : x) >> 4;
    for (UWORD row = 0; row < rows; row++) {
        UWORD m0 = m[row*2],   m1 = m[row*2+1];
        UWORD h0 = dh[row*2],  h1 = dh[row*2+1];
        UWORD l0 = dl[row*2],  l1 = dl[row*2+1];
        UWORD mv0 = m0 >> shift;
        UWORD mv1 = shift ? (UWORD)((m0 << (16-shift)) | (m1 >> shift)) : m1;
        UWORD mv2 = shift ? (UWORD)(m1 << (16-shift)) : 0;
        UWORD hv0 = h0 >> shift;
        UWORD hv1 = shift ? (UWORD)((h0 << (16-shift)) | (h1 >> shift)) : h1;
        UWORD hv2 = shift ? (UWORD)(h1 << (16-shift)) : 0;
        UWORD lv0 = l0 >> shift;
        UWORD lv1 = shift ? (UWORD)((l0 << (16-shift)) | (l1 >> shift)) : l1;
        UWORD lv2 = shift ? (UWORD)(l1 << (16-shift)) : 0;
        UWORD ry   = (UWORD)y + row;
        UWORD base = ry * (ROW_BYTES / 2) + wx;
        // Write planeHi (PF2 bit 0)
        UWORD* phi = (UWORD*)(screen_mem + planeHi * PLANE_BYTES);
        if (base < (ROW_BYTES/2) * SCREEN_H) {
            phi[base] = (UWORD)((phi[base] & ~mv0) | (hv0 & mv0));
        }
        if (wx + 1 < ROW_BYTES / 2) {
            phi[base+1] = (UWORD)((phi[base+1] & ~mv1) | (hv1 & mv1));
        }
        if (wx + 2 < ROW_BYTES / 2) {
            phi[base+2] = (UWORD)((phi[base+2] & ~mv2) | (hv2 & mv2));
        }
        // Write planeLo (PF2 bit 1)
        UWORD* plo = (UWORD*)(screen_mem + planeLo * PLANE_BYTES);
        if (base < (ROW_BYTES/2) * SCREEN_H) {
            plo[base] = (UWORD)((plo[base] & ~mv0) | (lv0 & mv0));
        }
        if (wx + 1 < ROW_BYTES / 2) {
            plo[base+1] = (UWORD)((plo[base+1] & ~mv1) | (lv1 & mv1));
        }
        if (wx + 2 < ROW_BYTES / 2) {
            plo[base+2] = (UWORD)((plo[base+2] & ~mv2) | (lv2 & mv2));
        }
        // Clear plane 5 (PF2 bit 2) in mask area — only if not a data plane
        if (planeHi != 5 && planeLo != 5) {
            UWORD* pl5 = (UWORD*)(screen_mem + 5 * PLANE_BYTES);
            if (base < (ROW_BYTES/2) * SCREEN_H) pl5[base] &= ~mv0;
            if (wx + 1 < ROW_BYTES / 2) pl5[base+1] &= ~mv1;
            if (wx + 2 < ROW_BYTES / 2) pl5[base+2] &= ~mv2;
        }
    }
}

// Draw 16px-wide bob with 2 independent data bitplanes (PF2 only)
// OR body bits into BPL4 without clearing anything (for black bullets:
// body on BPL2+BPL4 = reg 11 (per-pol: blue or red), accent stays BPL4-only = black).
static void OrBPL4(UBYTE* screen_mem, const UWORD* data,
                    short x, short y, UWORD rows) {
    if (x <= -16 || x >= SCREEN_W || y <= -(short)rows || y >= SCREEN_H) return;
    UWORD shift = (UWORD)(x & 15);
    if (y < 0) {
        UWORD skip = (UWORD)(-y);
        data += skip;
        rows = (rows > skip) ? rows - skip : 0;
        y = 0;
    }
    if (y + (short)rows > SCREEN_H)
        rows = (UWORD)(SCREEN_H - y);
    if (rows == 0) return;
    UWORD wx = (UWORD)(x < 0 ? 0 : x) >> 4;
    UWORD* pl = (UWORD*)(screen_mem + 3 * PLANE_BYTES);
    for (UWORD row = 0; row < rows; row++) {
        UWORD w = data[row];
        UWORD b0 = w >> shift;
        UWORD b1 = shift ? (UWORD)(w << (16 - shift)) : 0;
        UWORD ry = (UWORD)y + row;
        UWORD base = ry * (ROW_BYTES / 2) + wx;
        pl[base] |= b0;
        if (wx + 1 < ROW_BYTES / 2)
            pl[base+1] |= b1;
    }
}

static void DrawBob16_2bpl(UBYTE* screen_mem,
                           const UWORD* mask, const UWORD* dataHi, const UWORD* dataLo,
                           short x, short y, UBYTE planeHi, UBYTE planeLo, UWORD rows) {
    if (!DrawBob16d2Asm(screen_mem, mask, dataHi, dataLo, x, y, planeHi, planeLo, rows))
        return;
    if (x <= -16 || x >= SCREEN_W || y <= -(short)rows || y >= SCREEN_H) return;
    UWORD shift = (UWORD)(x & 15);
    const UWORD* m = mask;
    const UWORD* dh = dataHi;
    const UWORD* dl = dataLo;
    if (y < 0) { m += (UWORD)(-y); dh += (UWORD)(-y); dl += (UWORD)(-y); rows = (UWORD)(rows+y); y = 0; }
    if (y + (short)rows > SCREEN_H) rows = (UWORD)(SCREEN_H - y);
    if (rows == 0) return;
    UWORD wx = (UWORD)(x < 0 ? 0 : x) >> 4;
    for (UWORD row = 0; row < rows; row++) {
        UWORD mw = m[row], hw = dh[row], lw = dl[row];
        UWORD mv0 = mw >> shift;
        UWORD mv1 = shift ? (UWORD)(mw << (16-shift)) : 0;
        UWORD hv0 = hw >> shift;
        UWORD hv1 = shift ? (UWORD)(hw << (16-shift)) : 0;
        UWORD lv0 = lw >> shift;
        UWORD lv1 = shift ? (UWORD)(lw << (16-shift)) : 0;
        UWORD ry   = (UWORD)y + row;
        UWORD base = ry * (ROW_BYTES / 2) + wx;
        // Write planeHi (PF2 bit 0)
        UWORD* phi = (UWORD*)(screen_mem + planeHi * PLANE_BYTES);
        phi[base]   = (UWORD)((phi[base]   & ~mv0) | (hv0 & mv0));
        if (wx + 1 < ROW_BYTES / 2)
            phi[base+1] = (UWORD)((phi[base+1] & ~mv1) | (hv1 & mv1));
        // Write planeLo (PF2 bit 1)
        UWORD* plo = (UWORD*)(screen_mem + planeLo * PLANE_BYTES);
        plo[base]   = (UWORD)((plo[base]   & ~mv0) | (lv0 & mv0));
        if (wx + 1 < ROW_BYTES / 2)
            plo[base+1] = (UWORD)((plo[base+1] & ~mv1) | (lv1 & mv1));
        // Clear plane 5 (PF2 bit 2) in mask area — only if not a data plane
        if (planeHi != 5 && planeLo != 5) {
            UWORD* pl5 = (UWORD*)(screen_mem + 5 * PLANE_BYTES);
            pl5[base]   &= ~mv0;
            if (wx + 1 < ROW_BYTES / 2)
                pl5[base+1] &= ~mv1;
        }
    }
}

// Draw pixel on PF2 only (planes 1,3,5)
// colorIdx: bit 0 -> plane 1, bit 1 -> plane 3, bit 2 -> plane 5
static void DrawPixel(UBYTE* screen_mem, short x, short y, UBYTE colorIdx) {
    if ((UWORD)x >= SCREEN_W || (UWORD)y >= SCREEN_H) return;
    UWORD off = (UWORD)y * ROW_BYTES + ((UWORD)x >> 3);
    UBYTE bit = (UBYTE)(0x80 >> ((UWORD)x & 7));
    if (colorIdx & 1) screen_mem[1 * PLANE_BYTES + off] |= bit;
    if (colorIdx & 2) screen_mem[3 * PLANE_BYTES + off] |= bit;
    if (colorIdx & 4) screen_mem[5 * PLANE_BYTES + off] |= bit;
}

// Draw ship (32x24) - dual-playfield
// Pol A (colorMode=0): p0→BPL2(blanc), p1→BPL6(blau), p2→BPL4(negre)
// Pol B (colorMode=1): p0→BPL2(blanc), p1→BPL2+BPL4(vermell via reg 11), p2→BPL4(negre)
//   p1 OR'd a BPL2 i BPL4 perque no solapa amb p0/p2.
static void DrawShipAnim(UBYTE* screen_mem, short x, short y, UBYTE colorMode) {
    if (x <= -32 || x >= SCREEN_W || y <= -24 || y >= SCREEN_H) return;
    UWORD shift = (UWORD)(x & 15);
    UWORD rows  = SHIP_W_HEIGHT;
    UWORD skip  = 0;
    if (y < 0) { skip = (UWORD)(-y); rows = (UWORD)(SHIP_W_HEIGHT - skip); y = 0; }
    if (y + (short)rows > SCREEN_H) rows = (UWORD)(SCREEN_H - y);
    if (rows == 0) return;
    UWORD wx = (UWORD)(x < 0 ? 0 : x) >> 4;
    UWORD rowWords = ROW_BYTES / 2;
    UWORD* pl1 = (UWORD*)(screen_mem + 1 * PLANE_BYTES);
    UWORD* pl3 = (UWORD*)(screen_mem + 3 * PLANE_BYTES);
    UWORD* pl5 = (UWORD*)(screen_mem + 5 * PLANE_BYTES);
    const UWORD* mask    = (colorMode == 0) ? SHIP_W_A_MASK    : SHIP_W_B_MASK;
    const UWORD* const* planes = (colorMode == 0) ? SHIP_W_A_PLANES : SHIP_W_B_PLANES;
    const UWORD* p0 = planes[0];  // white
    const UWORD* p1 = planes[1];  // coloured
    const UWORD* p2 = planes[2];  // dark
    for (UWORD row = 0; row < rows; row++) {
        UWORD ri   = row + skip;
        UWORD m0   = mask[ri*2],   m1 = mask[ri*2+1];
        UWORD mv0  = m0 >> shift;
        UWORD mv1  = shift ? (UWORD)((m0 << (16-shift)) | (m1 >> shift)) : m1;
        UWORD mv2  = shift ? (UWORD)(m1 << (16-shift)) : 0;
        UWORD ry   = (UWORD)y + row;
        UWORD base = ry * (ROW_BYTES / 2) + wx;
        UWORD w0   = p0[ri*2],   w1 = p0[ri*2+1];    // white
        UWORD m0d  = p1[ri*2],   m1d = p1[ri*2+1];   // colour
        UWORD d0   = p2[ri*2],   d1 = p2[ri*2+1];    // dark
        UWORD wv0 = w0 >> shift;
        UWORD wv1 = shift ? (UWORD)((w0 << (16-shift)) | (w1 >> shift)) : w1;
        UWORD wv2 = shift ? (UWORD)(w1 << (16-shift)) : 0;
        UWORD mv0d = m0d >> shift;
        UWORD mv1d = shift ? (UWORD)((m0d << (16-shift)) | (m1d >> shift)) : m1d;
        UWORD mv2d = shift ? (UWORD)(m1d << (16-shift)) : 0;
        UWORD dv0 = d0 >> shift;
        UWORD dv1 = shift ? (UWORD)((d0 << (16-shift)) | (d1 >> shift)) : d1;
        UWORD dv2 = shift ? (UWORD)(d1 << (16-shift)) : 0;
        // p0 → BPL2
        if (wx < rowWords) {
            pl1[base]   = (UWORD)((pl1[base]   & ~mv0) | (wv0 & mv0));
            pl3[base]   = (UWORD)((pl3[base]   & ~mv0) | (dv0 & mv0));
        }
        if (wx + 1 < rowWords) {
            pl1[base+1] = (UWORD)((pl1[base+1] & ~mv1) | (wv1 & mv1));
            pl3[base+1] = (UWORD)((pl3[base+1] & ~mv1) | (dv1 & mv1));
        }
        if (wx + 2 < rowWords) {
            pl1[base+2] = (UWORD)((pl1[base+2] & ~mv2) | (wv2 & mv2));
            pl3[base+2] = (UWORD)((pl3[base+2] & ~mv2) | (dv2 & mv2));
        }
        if (colorMode == 0) {
            // Pol A: p1 → BPL6 (blau)
            if (wx < rowWords) {
                pl5[base]   = (UWORD)((pl5[base]   & ~mv0) | (mv0d & mv0));
            }
            if (wx + 1 < rowWords) {
                pl5[base+1] = (UWORD)((pl5[base+1] & ~mv1) | (mv1d & mv1));
            }
            if (wx + 2 < rowWords) {
                pl5[base+2] = (UWORD)((pl5[base+2] & ~mv2) | (mv2d & mv2));
            }
        } else {
            // Pol B: p1 → BPL2+BPL4 (vermell via reg 11)
            if (wx < rowWords) {
                pl1[base]   |= mv0d & mv0;
                pl3[base]   |= mv0d & mv0;
                pl5[base]   &= ~mv0;
            }
            if (wx + 1 < rowWords) {
                pl1[base+1] |= mv1d & mv1;
                pl3[base+1] |= mv1d & mv1;
                pl5[base+1] &= ~mv1;
            }
            if (wx + 2 < rowWords) {
                pl1[base+2] |= mv2d & mv2;
                pl3[base+2] |= mv2d & mv2;
                pl5[base+2] &= ~mv2;
            }
        }
    }
}

// Force field: circular ring (1 BPL thick) drawn into BPL6 of PF2.
// All ring masks are 48x48 with the ring centered at (24,24). When drawn at
// (g_ShipX-8, g_ShipY-12) the ring's center coincides with the ship's center.
// Visible color is g_Palette[12] (PF2 color 4 = BPL6=1).
//
// g_FFBubScroll0..3: banded-dither disc (r=22) that scrolls vertically.
// Implemented as 4 static frames, each one shifted by 1 row. Cycling them at
// 6 fps gives the "energy lines flowing across the sphere" look (Sonic 1's
// water bubble effect). The bands are 3 dense rows + 3 sparse rows = 6-row
// cycle, so 4 frames = one full band cycle.
// g_FFSweepSrc0..3 / g_FFSweepDst0..3: solid disc (r=22) split by a vertical
// "wiper" at x=2,16,30,44. Used during polarity-flip transitions to draw an
// OPAQUE dome that hides the ship. BPL4 gets the source mask (old polarity
// color), BPL6 gets the destination mask (new polarity color). By swapping
// the BPL assignments and reversing the frame order, the same masks are
// reused for both white->black (sweep left->right) and black->white (sweep
// right->left). Drawn AFTER the ship so the dome covers it.
#define FORCEFIELD_W 48
#define FORCEFIELD_H 48

static const UWORD g_FFSweepSrc0[FORCEFIELD_H*3] = { // frame 0: wiper at x=2
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0080, 0x0000,
    0x0000, 0x3FFE, 0x0000,
    0x0001, 0xFFFF, 0xC000,
    0x0007, 0xFFFF, 0xF000,
    0x000F, 0xFFFF, 0xF800,
    0x001F, 0xFFFF, 0xFC00,
    0x007F, 0xFFFF, 0xFF00,
    0x00FF, 0xFFFF, 0xFF80,
    0x00FF, 0xFFFF, 0xFF80,
    0x01FF, 0xFFFF, 0xFFC0,
    0x03FF, 0xFFFF, 0xFFE0,
    0x07FF, 0xFFFF, 0xFFF0,
    0x07FF, 0xFFFF, 0xFFF0,
    0x0FFF, 0xFFFF, 0xFFF8,
    0x0FFF, 0xFFFF, 0xFFF8,
    0x0FFF, 0xFFFF, 0xFFF8,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x3FFF, 0xFFFF, 0xFFFE,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x0FFF, 0xFFFF, 0xFFF8,
    0x0FFF, 0xFFFF, 0xFFF8,
    0x0FFF, 0xFFFF, 0xFFF8,
    0x07FF, 0xFFFF, 0xFFF0,
    0x07FF, 0xFFFF, 0xFFF0,
    0x03FF, 0xFFFF, 0xFFE0,
    0x01FF, 0xFFFF, 0xFFC0,
    0x00FF, 0xFFFF, 0xFF80,
    0x00FF, 0xFFFF, 0xFF80,
    0x007F, 0xFFFF, 0xFF00,
    0x001F, 0xFFFF, 0xFC00,
    0x000F, 0xFFFF, 0xF800,
    0x0007, 0xFFFF, 0xF000,
    0x0001, 0xFFFF, 0xC000,
    0x0000, 0x3FFE, 0x0000,
    0x0000, 0x0080, 0x0000,
    0x0000, 0x0000, 0x0000,
};
static const UWORD g_FFSweepDst0[FORCEFIELD_H*3] = { // frame 0
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
};

static const UWORD g_FFSweepSrc1[FORCEFIELD_H*3] = { // frame 1: wiper at x=16
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0080, 0x0000,
    0x0000, 0x3FFE, 0x0000,
    0x0000, 0xFFFF, 0xC000,
    0x0000, 0xFFFF, 0xF000,
    0x0000, 0xFFFF, 0xF800,
    0x0000, 0xFFFF, 0xFC00,
    0x0000, 0xFFFF, 0xFF00,
    0x0000, 0xFFFF, 0xFF80,
    0x0000, 0xFFFF, 0xFF80,
    0x0000, 0xFFFF, 0xFFC0,
    0x0000, 0xFFFF, 0xFFE0,
    0x0000, 0xFFFF, 0xFFF0,
    0x0000, 0xFFFF, 0xFFF0,
    0x0000, 0xFFFF, 0xFFF8,
    0x0000, 0xFFFF, 0xFFF8,
    0x0000, 0xFFFF, 0xFFF8,
    0x0000, 0xFFFF, 0xFFFC,
    0x0000, 0xFFFF, 0xFFFC,
    0x0000, 0xFFFF, 0xFFFC,
    0x0000, 0xFFFF, 0xFFFC,
    0x0000, 0xFFFF, 0xFFFC,
    0x0000, 0xFFFF, 0xFFFC,
    0x0000, 0xFFFF, 0xFFFE,
    0x0000, 0xFFFF, 0xFFFC,
    0x0000, 0xFFFF, 0xFFFC,
    0x0000, 0xFFFF, 0xFFFC,
    0x0000, 0xFFFF, 0xFFFC,
    0x0000, 0xFFFF, 0xFFFC,
    0x0000, 0xFFFF, 0xFFFC,
    0x0000, 0xFFFF, 0xFFF8,
    0x0000, 0xFFFF, 0xFFF8,
    0x0000, 0xFFFF, 0xFFF8,
    0x0000, 0xFFFF, 0xFFF0,
    0x0000, 0xFFFF, 0xFFF0,
    0x0000, 0xFFFF, 0xFFE0,
    0x0000, 0xFFFF, 0xFFC0,
    0x0000, 0xFFFF, 0xFF80,
    0x0000, 0xFFFF, 0xFF80,
    0x0000, 0xFFFF, 0xFF00,
    0x0000, 0xFFFF, 0xFC00,
    0x0000, 0xFFFF, 0xF800,
    0x0000, 0xFFFF, 0xF000,
    0x0000, 0xFFFF, 0xC000,
    0x0000, 0x3FFE, 0x0000,
    0x0000, 0x0080, 0x0000,
    0x0000, 0x0000, 0x0000,
};
static const UWORD g_FFSweepDst1[FORCEFIELD_H*3] = { // frame 1
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0001, 0x0000, 0x0000,
    0x0007, 0x0000, 0x0000,
    0x000F, 0x0000, 0x0000,
    0x001F, 0x0000, 0x0000,
    0x007F, 0x0000, 0x0000,
    0x00FF, 0x0000, 0x0000,
    0x00FF, 0x0000, 0x0000,
    0x01FF, 0x0000, 0x0000,
    0x03FF, 0x0000, 0x0000,
    0x07FF, 0x0000, 0x0000,
    0x07FF, 0x0000, 0x0000,
    0x0FFF, 0x0000, 0x0000,
    0x0FFF, 0x0000, 0x0000,
    0x0FFF, 0x0000, 0x0000,
    0x1FFF, 0x0000, 0x0000,
    0x1FFF, 0x0000, 0x0000,
    0x1FFF, 0x0000, 0x0000,
    0x1FFF, 0x0000, 0x0000,
    0x1FFF, 0x0000, 0x0000,
    0x1FFF, 0x0000, 0x0000,
    0x3FFF, 0x0000, 0x0000,
    0x1FFF, 0x0000, 0x0000,
    0x1FFF, 0x0000, 0x0000,
    0x1FFF, 0x0000, 0x0000,
    0x1FFF, 0x0000, 0x0000,
    0x1FFF, 0x0000, 0x0000,
    0x1FFF, 0x0000, 0x0000,
    0x0FFF, 0x0000, 0x0000,
    0x0FFF, 0x0000, 0x0000,
    0x0FFF, 0x0000, 0x0000,
    0x07FF, 0x0000, 0x0000,
    0x07FF, 0x0000, 0x0000,
    0x03FF, 0x0000, 0x0000,
    0x01FF, 0x0000, 0x0000,
    0x00FF, 0x0000, 0x0000,
    0x00FF, 0x0000, 0x0000,
    0x007F, 0x0000, 0x0000,
    0x001F, 0x0000, 0x0000,
    0x000F, 0x0000, 0x0000,
    0x0007, 0x0000, 0x0000,
    0x0001, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
};

static const UWORD g_FFSweepSrc2[FORCEFIELD_H*3] = { // frame 2: wiper at x=30
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0002, 0x0000,
    0x0000, 0x0003, 0xC000,
    0x0000, 0x0003, 0xF000,
    0x0000, 0x0003, 0xF800,
    0x0000, 0x0003, 0xFC00,
    0x0000, 0x0003, 0xFF00,
    0x0000, 0x0003, 0xFF80,
    0x0000, 0x0003, 0xFF80,
    0x0000, 0x0003, 0xFFC0,
    0x0000, 0x0003, 0xFFE0,
    0x0000, 0x0003, 0xFFF0,
    0x0000, 0x0003, 0xFFF0,
    0x0000, 0x0003, 0xFFF8,
    0x0000, 0x0003, 0xFFF8,
    0x0000, 0x0003, 0xFFF8,
    0x0000, 0x0003, 0xFFFC,
    0x0000, 0x0003, 0xFFFC,
    0x0000, 0x0003, 0xFFFC,
    0x0000, 0x0003, 0xFFFC,
    0x0000, 0x0003, 0xFFFC,
    0x0000, 0x0003, 0xFFFC,
    0x0000, 0x0003, 0xFFFE,
    0x0000, 0x0003, 0xFFFC,
    0x0000, 0x0003, 0xFFFC,
    0x0000, 0x0003, 0xFFFC,
    0x0000, 0x0003, 0xFFFC,
    0x0000, 0x0003, 0xFFFC,
    0x0000, 0x0003, 0xFFFC,
    0x0000, 0x0003, 0xFFF8,
    0x0000, 0x0003, 0xFFF8,
    0x0000, 0x0003, 0xFFF8,
    0x0000, 0x0003, 0xFFF0,
    0x0000, 0x0003, 0xFFF0,
    0x0000, 0x0003, 0xFFE0,
    0x0000, 0x0003, 0xFFC0,
    0x0000, 0x0003, 0xFF80,
    0x0000, 0x0003, 0xFF80,
    0x0000, 0x0003, 0xFF00,
    0x0000, 0x0003, 0xFC00,
    0x0000, 0x0003, 0xF800,
    0x0000, 0x0003, 0xF000,
    0x0000, 0x0003, 0xC000,
    0x0000, 0x0002, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
};
static const UWORD g_FFSweepDst2[FORCEFIELD_H*3] = { // frame 2
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0080, 0x0000,
    0x0000, 0x3FFC, 0x0000,
    0x0001, 0xFFFC, 0x0000,
    0x0007, 0xFFFC, 0x0000,
    0x000F, 0xFFFC, 0x0000,
    0x001F, 0xFFFC, 0x0000,
    0x007F, 0xFFFC, 0x0000,
    0x00FF, 0xFFFC, 0x0000,
    0x00FF, 0xFFFC, 0x0000,
    0x01FF, 0xFFFC, 0x0000,
    0x03FF, 0xFFFC, 0x0000,
    0x07FF, 0xFFFC, 0x0000,
    0x07FF, 0xFFFC, 0x0000,
    0x0FFF, 0xFFFC, 0x0000,
    0x0FFF, 0xFFFC, 0x0000,
    0x0FFF, 0xFFFC, 0x0000,
    0x1FFF, 0xFFFC, 0x0000,
    0x1FFF, 0xFFFC, 0x0000,
    0x1FFF, 0xFFFC, 0x0000,
    0x1FFF, 0xFFFC, 0x0000,
    0x1FFF, 0xFFFC, 0x0000,
    0x1FFF, 0xFFFC, 0x0000,
    0x3FFF, 0xFFFC, 0x0000,
    0x1FFF, 0xFFFC, 0x0000,
    0x1FFF, 0xFFFC, 0x0000,
    0x1FFF, 0xFFFC, 0x0000,
    0x1FFF, 0xFFFC, 0x0000,
    0x1FFF, 0xFFFC, 0x0000,
    0x1FFF, 0xFFFC, 0x0000,
    0x0FFF, 0xFFFC, 0x0000,
    0x0FFF, 0xFFFC, 0x0000,
    0x0FFF, 0xFFFC, 0x0000,
    0x07FF, 0xFFFC, 0x0000,
    0x07FF, 0xFFFC, 0x0000,
    0x03FF, 0xFFFC, 0x0000,
    0x01FF, 0xFFFC, 0x0000,
    0x00FF, 0xFFFC, 0x0000,
    0x00FF, 0xFFFC, 0x0000,
    0x007F, 0xFFFC, 0x0000,
    0x001F, 0xFFFC, 0x0000,
    0x000F, 0xFFFC, 0x0000,
    0x0007, 0xFFFC, 0x0000,
    0x0001, 0xFFFC, 0x0000,
    0x0000, 0x3FFC, 0x0000,
    0x0000, 0x0080, 0x0000,
    0x0000, 0x0000, 0x0000,
};

static const UWORD g_FFSweepSrc3[FORCEFIELD_H*3] = { // frame 3: wiper at x=44
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0008,
    0x0000, 0x0000, 0x0008,
    0x0000, 0x0000, 0x0008,
    0x0000, 0x0000, 0x000C,
    0x0000, 0x0000, 0x000C,
    0x0000, 0x0000, 0x000C,
    0x0000, 0x0000, 0x000C,
    0x0000, 0x0000, 0x000C,
    0x0000, 0x0000, 0x000C,
    0x0000, 0x0000, 0x000E,
    0x0000, 0x0000, 0x000C,
    0x0000, 0x0000, 0x000C,
    0x0000, 0x0000, 0x000C,
    0x0000, 0x0000, 0x000C,
    0x0000, 0x0000, 0x000C,
    0x0000, 0x0000, 0x000C,
    0x0000, 0x0000, 0x0008,
    0x0000, 0x0000, 0x0008,
    0x0000, 0x0000, 0x0008,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
};
static const UWORD g_FFSweepDst3[FORCEFIELD_H*3] = { // frame 3
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0080, 0x0000,
    0x0000, 0x3FFE, 0x0000,
    0x0001, 0xFFFF, 0xC000,
    0x0007, 0xFFFF, 0xF000,
    0x000F, 0xFFFF, 0xF800,
    0x001F, 0xFFFF, 0xFC00,
    0x007F, 0xFFFF, 0xFF00,
    0x00FF, 0xFFFF, 0xFF80,
    0x00FF, 0xFFFF, 0xFF80,
    0x01FF, 0xFFFF, 0xFFC0,
    0x03FF, 0xFFFF, 0xFFE0,
    0x07FF, 0xFFFF, 0xFFF0,
    0x07FF, 0xFFFF, 0xFFF0,
    0x0FFF, 0xFFFF, 0xFFF0,
    0x0FFF, 0xFFFF, 0xFFF0,
    0x0FFF, 0xFFFF, 0xFFF0,
    0x1FFF, 0xFFFF, 0xFFF0,
    0x1FFF, 0xFFFF, 0xFFF0,
    0x1FFF, 0xFFFF, 0xFFF0,
    0x1FFF, 0xFFFF, 0xFFF0,
    0x1FFF, 0xFFFF, 0xFFF0,
    0x1FFF, 0xFFFF, 0xFFF0,
    0x3FFF, 0xFFFF, 0xFFF0,
    0x1FFF, 0xFFFF, 0xFFF0,
    0x1FFF, 0xFFFF, 0xFFF0,
    0x1FFF, 0xFFFF, 0xFFF0,
    0x1FFF, 0xFFFF, 0xFFF0,
    0x1FFF, 0xFFFF, 0xFFF0,
    0x1FFF, 0xFFFF, 0xFFF0,
    0x0FFF, 0xFFFF, 0xFFF0,
    0x0FFF, 0xFFFF, 0xFFF0,
    0x0FFF, 0xFFFF, 0xFFF0,
    0x07FF, 0xFFFF, 0xFFF0,
    0x07FF, 0xFFFF, 0xFFF0,
    0x03FF, 0xFFFF, 0xFFE0,
    0x01FF, 0xFFFF, 0xFFC0,
    0x00FF, 0xFFFF, 0xFF80,
    0x00FF, 0xFFFF, 0xFF80,
    0x007F, 0xFFFF, 0xFF00,
    0x001F, 0xFFFF, 0xFC00,
    0x000F, 0xFFFF, 0xF800,
    0x0007, 0xFFFF, 0xF000,
    0x0001, 0xFFFF, 0xC000,
    0x0000, 0x3FFE, 0x0000,
    0x0000, 0x0080, 0x0000,
    0x0000, 0x0000, 0x0000,
};

// Pointer arrays for indexed access to the masks (4 frames each).
static const UWORD* const g_FFSweepSrcSet[4] = {
    g_FFSweepSrc0, g_FFSweepSrc1, g_FFSweepSrc2, g_FFSweepSrc3
};
static const UWORD* const g_FFSweepDstSet[4] = {
    g_FFSweepDst0, g_FFSweepDst1, g_FFSweepDst2, g_FFSweepDst3
};

// g_FFBubScroll0..3: banded-dither disc (r=22) that scrolls vertically.
// 3 dense rows + 3 sparse rows = 6-row band cycle. 4 masks = one full cycle,
// each shifted 1 row. Drawn into BPL6 only (normal mode).
static const UWORD g_FFBubScroll0[FORCEFIELD_H*3] = {
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0080, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0xAAAA, 0x8000,
    0x0000, 0x0000, 0x0000,
    0x000F, 0xFFFF, 0xF800,
    0x0015, 0x5555, 0x5400,
    0x007F, 0xFFFF, 0xFF00,
    0x0000, 0x0000, 0x0000,
    0x00AA, 0xAAAA, 0xAA80,
    0x0000, 0x0000, 0x0000,
    0x03FF, 0xFFFF, 0xFFE0,
    0x0555, 0x5555, 0x5550,
    0x07FF, 0xFFFF, 0xFFF0,
    0x0000, 0x0000, 0x0000,
    0x0AAA, 0xAAAA, 0xAAA8,
    0x0000, 0x0000, 0x0000,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x1555, 0x5555, 0x5554,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x0000, 0x0000, 0x0000,
    0x0AAA, 0xAAAA, 0xAAA8,
    0x0000, 0x0000, 0x0000,
    0x3FFF, 0xFFFF, 0xFFFE,
    0x1555, 0x5555, 0x5554,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x0000, 0x0000, 0x0000,
    0x0AAA, 0xAAAA, 0xAAA8,
    0x0000, 0x0000, 0x0000,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x0555, 0x5555, 0x5550,
    0x0FFF, 0xFFFF, 0xFFF8,
    0x0000, 0x0000, 0x0000,
    0x02AA, 0xAAAA, 0xAAA0,
    0x0000, 0x0000, 0x0000,
    0x03FF, 0xFFFF, 0xFFE0,
    0x0155, 0x5555, 0x5540,
    0x00FF, 0xFFFF, 0xFF80,
    0x0000, 0x0000, 0x0000,
    0x002A, 0xAAAA, 0xAA00,
    0x0000, 0x0000, 0x0000,
    0x000F, 0xFFFF, 0xF800,
    0x0005, 0x5555, 0x5000,
    0x0001, 0xFFFF, 0xC000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0080, 0x0000,
    0x0000, 0x0000, 0x0000,
};

static const UWORD g_FFBubScroll1[FORCEFIELD_H*3] = {
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0080, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0xAAAA, 0x8000,
    0x0005, 0x5555, 0x5000,
    0x000F, 0xFFFF, 0xF800,
    0x0015, 0x5555, 0x5400,
    0x002A, 0xAAAA, 0xAA00,
    0x0000, 0x0000, 0x0000,
    0x00AA, 0xAAAA, 0xAA80,
    0x0155, 0x5555, 0x5540,
    0x03FF, 0xFFFF, 0xFFE0,
    0x0555, 0x5555, 0x5550,
    0x02AA, 0xAAAA, 0xAAA0,
    0x0000, 0x0000, 0x0000,
    0x0AAA, 0xAAAA, 0xAAA8,
    0x0555, 0x5555, 0x5550,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x1555, 0x5555, 0x5554,
    0x0AAA, 0xAAAA, 0xAAA8,
    0x0000, 0x0000, 0x0000,
    0x0AAA, 0xAAAA, 0xAAA8,
    0x1555, 0x5555, 0x5554,
    0x3FFF, 0xFFFF, 0xFFFE,
    0x1555, 0x5555, 0x5554,
    0x0AAA, 0xAAAA, 0xAAA8,
    0x0000, 0x0000, 0x0000,
    0x0AAA, 0xAAAA, 0xAAA8,
    0x1555, 0x5555, 0x5554,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x0555, 0x5555, 0x5550,
    0x0AAA, 0xAAAA, 0xAAA8,
    0x0000, 0x0000, 0x0000,
    0x02AA, 0xAAAA, 0xAAA0,
    0x0555, 0x5555, 0x5550,
    0x03FF, 0xFFFF, 0xFFE0,
    0x0155, 0x5555, 0x5540,
    0x00AA, 0xAAAA, 0xAA80,
    0x0000, 0x0000, 0x0000,
    0x002A, 0xAAAA, 0xAA00,
    0x0015, 0x5555, 0x5400,
    0x000F, 0xFFFF, 0xF800,
    0x0005, 0x5555, 0x5000,
    0x0000, 0xAAAA, 0x8000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0080, 0x0000,
    0x0000, 0x0000, 0x0000,
};

static const UWORD g_FFBubScroll2[FORCEFIELD_H*3] = {
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0080, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0001, 0xFFFF, 0xC000,
    0x0005, 0x5555, 0x5000,
    0x000F, 0xFFFF, 0xF800,
    0x0000, 0x0000, 0x0000,
    0x002A, 0xAAAA, 0xAA00,
    0x0000, 0x0000, 0x0000,
    0x00FF, 0xFFFF, 0xFF80,
    0x0155, 0x5555, 0x5540,
    0x03FF, 0xFFFF, 0xFFE0,
    0x0000, 0x0000, 0x0000,
    0x02AA, 0xAAAA, 0xAAA0,
    0x0000, 0x0000, 0x0000,
    0x0FFF, 0xFFFF, 0xFFF8,
    0x0555, 0x5555, 0x5550,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x0000, 0x0000, 0x0000,
    0x0AAA, 0xAAAA, 0xAAA8,
    0x0000, 0x0000, 0x0000,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x1555, 0x5555, 0x5554,
    0x3FFF, 0xFFFF, 0xFFFE,
    0x0000, 0x0000, 0x0000,
    0x0AAA, 0xAAAA, 0xAAA8,
    0x0000, 0x0000, 0x0000,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x1555, 0x5555, 0x5554,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x0000, 0x0000, 0x0000,
    0x0AAA, 0xAAAA, 0xAAA8,
    0x0000, 0x0000, 0x0000,
    0x07FF, 0xFFFF, 0xFFF0,
    0x0555, 0x5555, 0x5550,
    0x03FF, 0xFFFF, 0xFFE0,
    0x0000, 0x0000, 0x0000,
    0x00AA, 0xAAAA, 0xAA80,
    0x0000, 0x0000, 0x0000,
    0x007F, 0xFFFF, 0xFF00,
    0x0015, 0x5555, 0x5400,
    0x000F, 0xFFFF, 0xF800,
    0x0000, 0x0000, 0x0000,
    0x0000, 0xAAAA, 0x8000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0080, 0x0000,
    0x0000, 0x0000, 0x0000,
};

static const UWORD g_FFBubScroll3[FORCEFIELD_H*3] = {
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
    0x0000, 0x0080, 0x0000,
    0x0000, 0x1554, 0x0000,
    0x0001, 0xFFFF, 0xC000,
    0x0005, 0x5555, 0x5000,
    0x000A, 0xAAAA, 0xA800,
    0x0000, 0x0000, 0x0000,
    0x002A, 0xAAAA, 0xAA00,
    0x0055, 0x5555, 0x5500,
    0x00FF, 0xFFFF, 0xFF80,
    0x0155, 0x5555, 0x5540,
    0x02AA, 0xAAAA, 0xAAA0,
    0x0000, 0x0000, 0x0000,
    0x02AA, 0xAAAA, 0xAAA0,
    0x0555, 0x5555, 0x5550,
    0x0FFF, 0xFFFF, 0xFFF8,
    0x0555, 0x5555, 0x5550,
    0x0AAA, 0xAAAA, 0xAAA8,
    0x0000, 0x0000, 0x0000,
    0x0AAA, 0xAAAA, 0xAAA8,
    0x1555, 0x5555, 0x5554,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x1555, 0x5555, 0x5554,
    0x2AAA, 0xAAAA, 0xAAAA,
    0x0000, 0x0000, 0x0000,
    0x0AAA, 0xAAAA, 0xAAA8,
    0x1555, 0x5555, 0x5554,
    0x1FFF, 0xFFFF, 0xFFFC,
    0x1555, 0x5555, 0x5554,
    0x0AAA, 0xAAAA, 0xAAA8,
    0x0000, 0x0000, 0x0000,
    0x0AAA, 0xAAAA, 0xAAA8,
    0x0555, 0x5555, 0x5550,
    0x07FF, 0xFFFF, 0xFFF0,
    0x0555, 0x5555, 0x5550,
    0x02AA, 0xAAAA, 0xAAA0,
    0x0000, 0x0000, 0x0000,
    0x00AA, 0xAAAA, 0xAA80,
    0x0055, 0x5555, 0x5500,
    0x007F, 0xFFFF, 0xFF00,
    0x0015, 0x5555, 0x5400,
    0x000A, 0xAAAA, 0xA800,
    0x0000, 0x0000, 0x0000,
    0x0000, 0xAAAA, 0x8000,
    0x0000, 0x1554, 0x0000,
    0x0000, 0x0080, 0x0000,
    0x0000, 0x0000, 0x0000,
};

static const UWORD* const g_FFBubScrollSet[4] = {
    g_FFBubScroll0, g_FFBubScroll1, g_FFBubScroll2, g_FFBubScroll3
};

// g_FrameCounter must be declared before DrawForceField uses it (for the
// scrolling-bubble animation). The real definition is here so the function
// can see it; GAME STATE code below can still access it normally.
__attribute__((externally_visible)) volatile short g_FrameCounter = 0;


// Draw a 48x48 1-bit mask into the given bitplane (3=BPL4, 5=BPL6, etc.).
// The mask is ORed into the destination plane. Multiple calls with different
// planes can be used to combine BPL4 + BPL6 (e.g. for the polarity-sweep).
static void DrawForceFieldMask(UBYTE* screen_mem, short x, short y,
                              const UWORD* mask, int planeIdx) {
    if (x <= -FORCEFIELD_W || x >= SCREEN_W || y <= -FORCEFIELD_H || y >= SCREEN_H) return;
    UWORD shift = (UWORD)(x & 15);
    UWORD rows  = FORCEFIELD_H;
    UWORD skip  = 0;
    if (y < 0) { skip = (UWORD)(-y); rows = (UWORD)(FORCEFIELD_H - skip); y = 0; }
    if (y + (short)rows > SCREEN_H) rows = (UWORD)(SCREEN_H - y);
    if (rows == 0) return;
    UWORD wx = (UWORD)(x < 0 ? 0 : x) >> 4;
    UWORD rowWords = ROW_BYTES / 2;
    UWORD* plane = (UWORD*)(screen_mem + planeIdx * PLANE_BYTES);
    for (UWORD row = 0; row < rows; row++) {
        UWORD ri   = row + skip;
        UWORD m0 = mask[ri*3], m1 = mask[ri*3+1], m2 = mask[ri*3+2];
        UWORD mv0 = m0 >> shift;
        UWORD mv1 = shift ? (UWORD)((m0 << (16-shift)) | (m1 >> shift)) : m1;
        UWORD mv2 = shift ? (UWORD)((m1 << (16-shift)) | (m2 >> shift)) : m2;
        UWORD ry   = (UWORD)y + row;
        UWORD base = ry * rowWords + wx;
        if (wx < rowWords) plane[base] |= mv0;
        if (wx + 1 < rowWords) plane[base+1] |= mv1;
        if (wx + 2 < rowWords) plane[base+2] |= mv2;
        if (shift > 0 && wx + 3 < rowWords) {
            plane[base+3] |= m2 << (16 - shift);
        }
    }
}

// Draw the force field. Two modes:
//   - Normal  (transition == 0): scrolling banded-dither bubble.
//                                 Pol A: BPL6 (reg 12 = blue)
//                                 Pol B: BPL2+BPL4 (reg 11 = red)
//   - Sweep   (transition > 0):  OPAQUE disc split by a vertical wiper.
//                                 white→black: source BPL6 (blue) sweeping out,
//                                              dest BPL2+BPL4 (red) sweeping in.
//                                 black→white: source BPL2+BPL4 (red) sweeping out,
//                                              dest BPL6 (blue) sweeping in.
static void DrawForceField(UBYTE* screen_mem, short shipX, short shipY,
                           short transition, short sweepDir, short polarity) {
    short mx = (short)(shipX - 8);
    short my = (short)(shipY - 12);

    if (transition > 0) {
        short phase = 4 - transition;
        if (phase < 0) phase = 0;
        if (phase > 3) phase = 3;
        short idx = (sweepDir == 0) ? phase : (3 - phase);
        // Paleta fixa: BPL6=blau, BPL2+BPL4=vermell.
        // El wiper separa source (vella) i dest (nova).
        if (sweepDir == 0) {
            // white→black: old=blue(BPL6) sweeping out, new=red(BPL2+BPL4) sweeping in
            DrawForceFieldMask(screen_mem, mx, my, g_FFSweepSrcSet[idx], 5); // BPL6
            DrawForceFieldMask(screen_mem, mx, my, g_FFSweepDstSet[idx], 3); // BPL4
            DrawForceFieldMask(screen_mem, mx, my, g_FFSweepDstSet[idx], 1); // BPL2
        } else {
            // black→white: old=red(BPL2+BPL4) sweeping out, new=blue(BPL6) sweeping in
            DrawForceFieldMask(screen_mem, mx, my, g_FFSweepSrcSet[idx], 3); // BPL4
            DrawForceFieldMask(screen_mem, mx, my, g_FFSweepSrcSet[idx], 1); // BPL2
            DrawForceFieldMask(screen_mem, mx, my, g_FFSweepDstSet[idx], 5); // BPL6
        }
    } else {
        // Normal scrolling dome — plane segons polaritat
        short scroll = (g_FrameCounter >> 3) & 3;
        if (polarity == 0) {
            DrawForceFieldMask(screen_mem, mx, my, g_FFBubScrollSet[scroll], 5);  // BPL6 = blue
        } else {
            DrawForceFieldMask(screen_mem, mx, my, g_FFBubScrollSet[scroll], 3);  // BPL4
            DrawForceFieldMask(screen_mem, mx, my, g_FFBubScrollSet[scroll], 1);  // BPL2 → BPL2+BPL4 = red
        }
    }
}

// ============================================================================
// COPPER LIST HELPERS
// ============================================================================

__attribute__((always_inline)) inline USHORT* copSetColor(USHORT* p, USHORT idx, USHORT col) {
    *p++ = 0x180 + sizeof(UWORD) * idx;  // absolute copper COLORxx register
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
static UWORD g_Palette[32] = {
    // PF1 (background): slots 0-7 — tilemap colors loaded from level asset
    0x0000,              //  0
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,  //  1-6
    0x0000,              //  7
    // PF2 (game sprites): slots 8-15 — TOTS FIXOS (mai canvien)
    //   reg 9  (BPL2):      0x0FFF white   — nau blanca, accent bala blanca
    //   reg 10 (BPL4):      0x000  black   — contorn nau, accent bala negra
    //   reg 11 (BPL2+BPL4): 0xF00  red     — cos bala negra, dom pol B, detall nau pol B
    //   reg 12 (BPL6):      0x0CF  blue    — cos bala blanca, dom pol A, detall nau pol A
    //   reg 13 (BPL2+BPL6): 0x0212         — border dark
    //   reg 14 (BPL4+BPL6): 0x0756         — border mid
    //   reg 15 (all):       0x0434         — border light
    0x0000,              //  8  PF2 transparent
    0x0FFF,              //  9  PF2 white
    0x000,               // 10  PF2 black
    0xF00,               // 11  PF2 red
    0x0CF,               // 12  PF2 blue
    0x0212,              // 13  PF2 border dark
    0x0756,              // 14  PF2 border mid
    0x0434,              // 15  PF2 border light
    // Slots 16-31: HW sprite colors (updated per frame by UpdateSpriteData)
    0x0000,              // 16  spare
    0x0FFF,              // 17  pair0 body (white)
    0x0CF,               // 18  pair0 accent (blue)
    0x0CF,               // 19  pair0 overlay
    0x0000,              // 20  spare
    0x0FFF,              // 21  pair1 body (white)
    0x0CF,               // 22  pair1 accent (blue)
    0x0CF,               // 23  pair1 overlay
    0x0000,              // 24  spare
    0x0000,              // 25  pair2 body (black)
    0xF00,               // 26  pair2 accent (red)
    0x0CF,               // 27  pair2 overlay
    0x0000,              // 28  spare
    0x0000,              // 29  pair3 body (black)
    0xF00,               // 30  pair3 accent (red)
    0x0CF,               // 31  pair3 overlay
};

// ============================================================================
// GAME STATE
// ============================================================================

static short g_GameState  = GS_PLAYING;
static short g_StarsEnabled = 0;  // 0=planet mode (no stars), 1=space mode
static short g_BGScrollY  = 0;    // tilemap vertical scroll offset
static short g_BorderScrollY = 0; // foreground border scroll

// Ship
static short g_ShipX, g_ShipY;
static short g_ShipExploding  = 0;
static short g_ShipExplTimer  = 0;

// Shots
__attribute__((externally_visible)) TShot      g_Shots[MAX_SHOTS];
static short      g_FireCooldown     = 0;
// Polarity: 0 = white (default), 1 = black (when fire is held)
static short      g_ShipPolarity     = 0;
static short      g_FireHoldFrames   = 0;
static short      g_FireWasPressed   = 0;

// Force field: rendered as a 1-bit BPL6 blit (rectangular ring around the ship).
// In dual-playfield mode OCS hardware sprites are drawn BEHIND both playfields,
// so a hardware-sprite approach is invisible. Using BPL6 (the 3rd plane of PF2,
// unused in the game area) keeps the ring on top of the ship + playfield pixels.
static short      g_ForceFieldTransition = 0;   // frames left in polarity sweep (4=just started, 0=done)
static short      g_ForceFieldSweepDir   = 0;   // 0=white->black (wiper L->R), 1=black->white (R->L)

// Enemies
__attribute__((externally_visible)) TEnemy     g_Enemies[MAX_ENEMIES];
__attribute__((externally_visible)) TEnemyShot g_EnemyShots[MAX_ENEMY_SHOTS];
__attribute__((externally_visible)) TExplosion g_Explosions[MAX_EXPLOSIONS];

// Absorption chain: counts absorbed shots of the same polarity. Capped at
// CHAIN_MAX. Displayed in the HUD as a row of small dots.
static short g_AbsorbCount = 0;

// Score / progress
static unsigned short g_Score      = 0;
static short          g_Lives      = 3;
static short          g_Level      = 1;
static unsigned short g_NextLifeAt = EXTRA_LIFE_EVERY;

// Wave state
static short g_WaveActive   = 0;
static short g_WaveTotal    = 0;
static short g_WaveSpawned  = 0;
__attribute__((externally_visible)) short g_WaveKilled   = 0;
static short g_SpawnTimer   = SPAWN_FIRST_DELAY;

// Starfield
static TStar *g_Stars1;
static TStar *g_Stars2;
static TStar *g_Stars3;

// Hi-scores
static THiScore *g_HiScores;

// ============================================================================
// LEVEL TABLE (25 levels, 1:1 with MSX/CPC)
// ============================================================================

static const TLevelConfig g_Levels[ENDGAME_FINAL_LEVEL] = {
    /* 1*/  { 3, 3, LMASK_BASIC,                       0 },
    /* 2*/  { 3, 4, LMASK_BASIC|LMASK_FAST,            0 },
    /* 3*/  { 4, 3, LMASK_BASIC|LMASK_FAST,            0 },
    /* 4*/  { 4, 4, LMASK_BASIC|LMASK_FAST,            0 },
    /* 5*/  { 1, 1, 0,                                 LCFG_F_BOSS1 },
    /* 6*/  { 4, 4, LMASK_BASIC|LMASK_FAST,            0 },
    /* 7*/  { 4, 4, LMASK_BASIC|LMASK_FAST,            0 },
    /* 8*/  { 5, 4, LMASK_BASIC|LMASK_FAST,            0 },
    /* 9*/  { 5, 5, LMASK_BASIC|LMASK_FAST,            0 },
    /*10*/  { 1, 1, 0,                                 LCFG_F_BOSS1 },
    /*11*/  { 5, 4, LMASK_BASIC|LMASK_FAST,            0 },
    /*12*/  { 5, 5, LMASK_BASIC|LMASK_FAST,            0 },
    /*13*/  { 6, 4, LMASK_BASIC|LMASK_FAST,            0 },
    /*14*/  { 6, 5, LMASK_BASIC|LMASK_FAST,            0 },
    /*15*/  { 1, 1, 0,                                 LCFG_F_BOSS1|LCFG_F_BOSS2 },
    /*16*/  { 6, 5, LMASK_BASIC|LMASK_FAST,            0 },
    /*17*/  { 7, 5, LMASK_BASIC|LMASK_FAST,            0 },
    /*18*/  { 7, 6, LMASK_BASIC|LMASK_FAST,            0 },
    /*19*/  { 7, 6, LMASK_BASIC|LMASK_FAST,            0 },
    /*20*/  { 1, 1, 0,                                 LCFG_F_BOSS1|LCFG_F_BOSS2 },
    /*21*/  { 8, 6, LMASK_BASIC|LMASK_FAST,            0 },
    /*22*/  { 8, 6, LMASK_BASIC|LMASK_FAST,            0 },
    /*23*/  { 8, 7, LMASK_BASIC|LMASK_FAST,            0 },
    /*24*/  { 8, 7, LMASK_BASIC|LMASK_FAST,            0 },
    /*25*/  { 1, 1, 0,                                 LCFG_F_BOSS1|LCFG_F_BOSS2 },
};

// ============================================================================
// STARFIELD
// ============================================================================

// Stars avoid wall zone: 16px padding on each side of game area
#define STAR_X0 (GAME_X0 + WALL_L_W)       // 32
#define STAR_W  (GAME_W - WALL_L_W * 2)    // 224
#define STAR_X1 (STAR_X0 + STAR_W)          // 256

static void InitStarfield() {
    g_Stars1 = (TStar*)AllocMem(sizeof(TStar) * N_STARS_1, MEMF_FAST);
    g_Stars2 = (TStar*)AllocMem(sizeof(TStar) * N_STARS_2, MEMF_FAST);
    g_Stars3 = (TStar*)AllocMem(sizeof(TStar) * N_STARS_3, MEMF_FAST);
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
    g_HiScores = (THiScore*)AllocMem(sizeof(THiScore) * HISCORE_COUNT, MEMF_FAST);
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
    g_FireCooldown    = 0;
    g_ShipPolarity    = 0;
    g_FireHoldFrames  = 0;
    g_FireWasPressed  = 0;
}

static void ResetGameSession() {
    g_Score      = 0;
    g_Lives      = 3;
    g_Level      = 1;
    g_NextLifeAt = EXTRA_LIFE_EVERY;
    g_BGScrollY     = BG_MAP_ROWS * BG_TILE_H - SCREEN_H;
    g_BorderScrollY = BORDER_H - SCREEN_H;
    for (int i = 0; i < MAX_ENEMIES;    i++) g_Enemies[i].active    = 0;
    for (int i = 0; i < MAX_ENEMY_SHOTS;i++) g_EnemyShots[i].active = 0;
    for (int i = 0; i < MAX_EXPLOSIONS; i++) g_Explosions[i].active = 0;
    g_AbsorbCount = 0;
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
    short available[2];
    short count = 0;
    for (short t = 0; t < 2; t++) {
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
    e->variant = (short)((g_FrameCounter + g_WaveSpawned) & 1);  // alternate white/black
    switch (type) {
        case ENEMY_TYPE_BASIC:  e->health = 1; e->vy = ENEMY_SPEED_BASIC;  break;
        case ENEMY_TYPE_FAST:   e->health = 1; e->vy = ENEMY_SPEED_FAST;   break;
    }
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

// Ikaruga-style absorption: an enemy shot of the same polarity as the ship
// is "caught" by the dome and the energy is banked into g_AbsorbCount
// (capped at CHAIN_MAX). The shot is consumed (no bounce-back) and the
// energy will be released later as a special attack (TBD: power shot or
// bomb). A brief flash marks the absorption point.
static void AbsorbEnemyShot(short enemyShotIdx) {
    short ex = g_EnemyShots[enemyShotIdx].x;
    short ey = g_EnemyShots[enemyShotIdx].y;
    g_EnemyShots[enemyShotIdx].active = 0;

    if (g_AbsorbCount < CHAIN_MAX) g_AbsorbCount++;

    // Brief visual feedback: small light-grey flash at the absorption point.
    SpawnExplosion(ex, ey, EXP_KIND_ENEMY);
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
    custom->copjmp1 = 0;  // strobe: force copper to reload COP1LC immediately
    if (g_SprDataPending && g_SprDataPending != g_SprDataActive) {
        g_SprDataActive = g_SprDataPending;
        g_SprDataPending = 0;
        g_SprDataBuild = (g_SprDataActive == g_SprDataA) ? g_SprDataB : g_SprDataA;
        for (int p = 0; p < SPR_MAX_PAIRS; p++)
            g_SprPairShotActive[p] = g_SprPairShotBuild[p];
    }
    // Set sprite pointers for all 8 channels — chip RAM has correct POS/CTL
    if (g_SprDataActive) {
        for (int s = 0; s < 8; s++) {
            ULONG addr = (ULONG)(g_SprDataActive + s * SPR_CHAN_WORDS);
            volatile USHORT* ptr = (volatile USHORT*)(0xDFF120 + s * 4);
            ptr[0] = (USHORT)(addr >> 16);
            ptr[1] = (USHORT)addr;
        }
    }
    g_FrameCounter++;
}

// ============================================================================
// COPPER LIST BUILD
// ============================================================================


static void BuildCopperListEx(USHORT* cop, const UBYTE** pf1_planes, const UBYTE** pf2_planes, short scroll_y) {
    const USHORT x     = 129;
    const USHORT width = 320;
    const USHORT height= 256;
    const USHORT y     = 44;
    const USHORT RES   = 8;
    USHORT xstop = x + width;
    USHORT ystop = y + height;
    USHORT fw    = (x >> 1) - RES;

    // Wait for VBlank (line 0) so all registers are set before visible area (line 44)
    *cop++ = 0x0001; *cop++ = 0xFFFE;

    cop = copSetReg(cop, 0x092, fw);                              // DDFSTRT
    cop = copSetReg(cop, 0x094, fw + (((width>>4)-1)<<3));       // DDFSTOP
    cop = copSetReg(cop, 0x08E, x + (y<<8));                      // DIWSTRT
    cop = copSetReg(cop, 0x090, (xstop-256) + ((ystop-256)<<8)); // DIWSTOP

    cop = copSetReg(cop, 0x100, (6<<12) | (1<<10));               // BPLCON0
    cop = copSetReg(cop, 0x102, 0);                                // BPLCON1
    cop = copSetReg(cop, 0x104, (1<<6) | (2<<2) | (1<<1));        // BPLCON2
    cop = copSetReg(cop, 0x108, 0);                                // BPL1MOD
    cop = copSetReg(cop, 0x10A, 0);                                // BPL2MOD

    // PF1: BPL1, BPL3, BPL5 — from bg_buf with vertical scroll
    // PF2: BPL2, BPL4, BPL6 — from draw_buf (no scroll)
    // Absolute copper register addresses (0x0E0 = BPL1PTH, 0x0E8 = BPL3PTH, 0x0F0 = BPL5PTH)
    //                           (0x0E4 = BPL2PTH, 0x0EC = BPL4PTH, 0x0F4 = BPL6PTH)
    static const USHORT pf1_regs[3] = { 0x0E0, 0x0E8, 0x0F0 };
    static const USHORT pf2_regs[3] = { 0x0E4, 0x0EC, 0x0F4 };
    for (int i = 0; i < 3; i++) {
        ULONG addr_pf1 = (ULONG)pf1_planes[i] + scroll_y * ROW_BYTES;
        ULONG addr_pf2 = (ULONG)pf2_planes[i];
        *cop++ = pf1_regs[i];
        *cop++ = (UWORD)(addr_pf1 >> 16);
        *cop++ = pf1_regs[i] + 2;
        *cop++ = (UWORD)addr_pf1;
        *cop++ = pf2_regs[i];
        *cop++ = (UWORD)(addr_pf2 >> 16);
        *cop++ = pf2_regs[i] + 2;
        *cop++ = (UWORD)addr_pf2;
    }

    for (int i = 0; i < 32; i++)
        cop = copSetColor(cop, i, g_Palette[i]);

    *cop++ = 0xffff;
    *cop++ = 0xfffe;
}

// ============================================================================
// MAIN
// ============================================================================

static void UpdateSpriteData(UWORD* sprData) {
    // Mark all pairs as unused in the build buffer
    for (int p = 0; p < SPR_MAX_PAIRS; p++)
        g_SprPairShotBuild[p] = -1;
    // Assign active shots to sprite pairs
    for (int i = 0; i < MAX_SHOTS; i++) {
        if (!g_Shots[i].active) continue;
        if (g_Shots[i].x < -SHOT_W || g_Shots[i].x >= SCREEN_W) continue;
        if (g_Shots[i].x > 126) continue;  // OCS HSTART wraps; draw with blitter fallback
        int pair = -1;
        for (int p = SPR_FIRST_USABLE_PAIR; p < SPR_FIRST_USABLE_PAIR + SPR_USABLE_PAIRS; p++) {
            if (g_SprPairShotBuild[p] < 0) {
                pair = p;
                break;
            }
        }
        if (pair < 0) continue;
        g_SprPairShotBuild[pair] = i;
        TShot* shot = &g_Shots[i];
        short hstart = 129 + shot->x;
        short vstart = 44 + shot->y;
        UWORD pos = ((UWORD)vstart << 8) | ((UWORD)hstart & 0xFE);
        UWORD vstop = (UWORD)(vstart + SPR_SHOT_ROWS) & 0x0F;
        UWORD evenCtl = (vstop << 8) | ((((UWORD)vstart >> 8) & 1) << 7) | ((UWORD)hstart & 1);
        UWORD oddCtl  = (vstop << 8) | (1 << 7) | ((UWORD)hstart & 1);
        UWORD* even = sprData + pair * 2 * SPR_CHAN_WORDS;
        UWORD* odd  = even + SPR_CHAN_WORDS;
        even[0] = pos;  even[1] = evenCtl;
        odd[0]  = pos;  odd[1]  = oddCtl;
        for (int r = 0; r < SPR_SHOT_ROWS; r++) {
            even[2+r] = g_ShotSpr_Body[r];
            odd[2+r]  = g_ShotSpr_Accent[r];
        }
        even[10] = 0; even[11] = 0;
        odd[10]  = 0; odd[11]  = 0;
    }
    // Mark unused pairs as off-screen — clear data words, set POS/CTL to VSTART=300
    for (int p = 0; p < SPR_MAX_PAIRS; p++) {
        if (g_SprPairShotBuild[p] >= 0) continue;
        UWORD* even = sprData + p * 2 * SPR_CHAN_WORDS;
        UWORD* odd  = even + SPR_CHAN_WORDS;
        short vstart = 300;
        short vstop  = (short)((vstart + SPR_SHOT_ROWS) & 0x0F);
        even[0] = ((USHORT)(vstart & 0xFF) << 8);                 // VSTART[7:0]
        even[1] = (UWORD)(vstop << 8) | ((((UWORD)vstart >> 8) & 1) << 7);
        odd[0]  = even[0];
        odd[1]  = (UWORD)(vstop << 8) | (1 << 7);                // ATT=1
        for (int w = 2; w < SPR_CHAN_WORDS; w++) {
            even[w] = 0;
            odd[w]  = 0;
        }
    }
    // Update sprite palette in g_Palette for attached pairs
    for (int p = 0; p < SPR_MAX_PAIRS; p++) {
        int idx = g_SprPairShotBuild[p];
        UWORD bodyColor = 0;
        UWORD accentColor = 0;
        UWORD overlayColor = 0;
        if (idx >= 0) {
            if (g_Shots[idx].variant == 0) {
                bodyColor = 0xFFF;
                accentColor = 0x0CF;
                overlayColor = 0x0CF;
            } else {
                bodyColor = 0x000;
                accentColor = 0xF00;
                overlayColor = 0xF00;
            }
        }
        if (p < SPR_MAX_PAIRS) {
            int baseReg = 17 + p * 4;
            if (baseReg + 2 < 32) {
                g_Palette[baseReg]   = bodyColor;
                g_Palette[baseReg+1] = accentColor;
                g_Palette[baseReg+2] = overlayColor;
            }
        }
    }
}

static void RenderFrame(UBYTE* screen_mem) {
    UpdateSpriteData(g_SprDataBuild);
    g_SprDataPending = g_SprDataBuild;
    ClearGameArea(screen_mem);
    DrawBorder(screen_mem, g_BorderScrollY);
    if (g_StarsEnabled) {
        for (int i = 0; i < N_STARS_1; i++) DrawPixel(screen_mem, g_Stars1[i].x, g_Stars1[i].y, 1);
        for (int i = 0; i < N_STARS_2; i++) DrawPixel(screen_mem, g_Stars2[i].x, g_Stars2[i].y, 2);
        for (int i = 0; i < N_STARS_3; i++) DrawPixel(screen_mem, g_Stars3[i].x, g_Stars3[i].y, 3);
    }
    if (g_GameState == GS_PLAYING || g_GameState == GS_GAMEOVER) {
        if (!g_ShipExploding) {
            if (g_ForceFieldTransition > 0) {
                // During a polarity sweep: draw the ship first, then the
                // OPAQUE dome on top so the ship is hidden by the wipe.
                UBYTE animFrame = (UBYTE)((g_FrameCounter >> 2) & 3);
                DrawShipAnim(screen_mem, g_ShipX, g_ShipY, (UBYTE)g_ShipPolarity);
                DrawForceField(screen_mem, g_ShipX, g_ShipY, g_ForceFieldTransition, g_ForceFieldSweepDir, g_ShipPolarity);
            } else {
                // Normal: scrolling bubble before the ship.
                DrawForceField(screen_mem, g_ShipX, g_ShipY, 0, 0, g_ShipPolarity);
                UBYTE animFrame = (UBYTE)((g_FrameCounter >> 2) & 3);
                DrawShipAnim(screen_mem, g_ShipX, g_ShipY, (UBYTE)g_ShipPolarity);
            }
        }
        for (int i = 0; i < MAX_ENEMIES; i++) {
            TEnemy* e = &g_Enemies[i];
            if (!e->active) continue;
            switch (e->type) {
                case ENEMY_TYPE_BASIC:
                    DrawBob32_2bpl(screen_mem, g_EnemyBasic24Mask,
                                   e->variant ? g_EnemyBasic24InvHi : g_EnemyBasic24Hi,
                                   e->variant ? g_EnemyBasic24InvLo : g_EnemyBasic24Lo,
                                   e->x, e->y, 1, 3);
                    break;
                case ENEMY_TYPE_FAST:
                    DrawBob16_2bpl(screen_mem, g_EnemyFast16Mask,
                                   e->variant ? g_EnemyFast16InvHi : g_EnemyFast16Hi,
                                   e->variant ? g_EnemyFast16InvLo : g_EnemyFast16Lo,
                                   e->x, e->y, 1, 3, 16);
                    break;
                default:
                    break;  // BOSS and unknown types handled elsewhere
            }
        }
        for (int i = 0; i < MAX_SHOTS; i++) {
            if (!g_Shots[i].active) continue;
            // Check if this shot has a HW sprite slot currently active.
            // If it is only planned for the next frame, draw it in software this frame.
            int hasSpr = 0;
            for (int p = 0; p < SPR_MAX_PAIRS; p++)
                if (g_SprPairShotActive[p] == i) { hasSpr = 1; break; }
            if (hasSpr) continue;
            short flip = g_FrameCounter & 1;
            if (g_Shots[i].variant == 0) {
                if (flip == 0) {
                    DrawBob16_2bpl(screen_mem, g_Shot16W_Mask, g_Shot16W_Accent, g_Shot16W_Body,
                                   g_Shots[i].x, g_Shots[i].y, 1, 5, 16);
                } else {
                    DrawBob16_2bpl(screen_mem, g_Shot16W_Mask, g_Shot16W_Body, g_Shot16W_Accent,
                                   g_Shots[i].x, g_Shots[i].y, 1, 5, 16);
                }
            } else {
                DrawBob16_2bpl(screen_mem, g_Shot16B_Mask, g_Shot16B_Accent, g_Shot16B_Body,
                               g_Shots[i].x, g_Shots[i].y, 3, 1, 16);
                OrBPL4(screen_mem, g_Shot16B_Body,
                       g_Shots[i].x, g_Shots[i].y, 16);
            }
        }
        for (int i = 0; i < MAX_ENEMY_SHOTS; i++) {
            if (!g_EnemyShots[i].active) continue;
            if (g_EnemyShots[i].variant == 0) {
                DrawBob16_2bpl(screen_mem, g_EShotW_Mask, g_EShotW_DataHi, g_EShotW_DataLo,
                               g_EnemyShots[i].x, g_EnemyShots[i].y, 1, 5, 4);
            } else {
                DrawBob16_2bpl(screen_mem, g_EShotB_Mask, g_EShotB_DataHi, g_EShotB_DataLo,
                               g_EnemyShots[i].x, g_EnemyShots[i].y, 3, 1, 4);
                OrBPL4(screen_mem, g_EShotB_DataLo,
                       g_EnemyShots[i].x, g_EnemyShots[i].y, 4);
            }
        }
        for (int i = 0; i < MAX_EXPLOSIONS; i++) {
            TExplosion* ex = &g_Explosions[i];
            if (!ex->active) continue;
            UWORD fr = (UWORD)((ex->frame >> 1) & 3);
            DrawBob16(screen_mem, g_ExpMasks[fr], g_ExpData[fr], ex->x, ex->y, 0x03, 8);
        }

        // HUD: absorption chain display. 3 small dots in the top-right corner.
        // Always white (reg 9 = 0x0FFF via BPL2), visible against black bg.
        {
            UBYTE dotColor = 0x01;  // BPL2 only = white
            short baseX = HUD_X + 8;
            short baseY = 8;
            for (int i = 0; i < CHAIN_MAX; i++) {
                short dx = (short)(baseX + i * 10);
                UBYTE c = (i < g_AbsorbCount) ? dotColor : 0x00;
                for (int py = 0; py < 4; py++)
                    for (int px = 0; px < 4; px++)
                        DrawPixel(screen_mem, (short)(dx + px), (short)(baseY + py), c);
            }
        }
    }
}

int main() {
    SysBase = *((struct ExecBase**)4UL);
    custom  = (struct Custom*)0xdff000;

    if (AvailMem(MEMF_CHIP) < 384*1024 || AvailMem(MEMF_FAST) < 256*1024) {
        KPrintF("Nau DX requires 1 MB RAM (512K chip + 512K slow)\n");
        Exit(0);
    }

    GfxBase = (struct GfxBase*)OpenLibrary((CONST_STRPTR)"graphics.library", 0);
    if (!GfxBase) Exit(0);

    DOSBase = (struct DosLibrary*)OpenLibrary((CONST_STRPTR)"dos.library", 0);
    if (!DOSBase) Exit(0);

    KPrintF("Nau DX 1MB edition\n");

    InitHiScores();
    ResetGameSession();

    // Load tilemap palette into PF1 slots 0-7
    for (int i = 0; i < BG_PAL_AMIGA_COUNT && i < 8; i++)
        g_Palette[i] = bg_pal_amiga[i];

    // --- Allocate screen memory: double buffer (6 bitplanes × 2) ---
    const ULONG plane_size = (SCREEN_W / 8) * SCREEN_H; // 320/8 * 256 = 10240 bytes
    const ULONG buf_size   = plane_size * SCREEN_BPL;    // 61440 bytes per buffer
    UBYTE* screen_mem = (UBYTE*)AllocMem(buf_size * 2, MEMF_CHIP | MEMF_CLEAR);
    if (!screen_mem) { CloseLibrary((struct Library*)DOSBase); CloseLibrary((struct Library*)GfxBase); Exit(0); }
    UBYTE* draw_buf = screen_mem;
    UBYTE* show_buf = screen_mem + buf_size;

    // --- Allocate pre-rendered background buffer (PF1 only, 3 planes × 1024 rows) ---
    UBYTE* bg_buf = (UBYTE*)AllocMem(BG_PLANE_BYTES * BG_BPL, MEMF_CHIP | MEMF_CLEAR);
    if (!bg_buf) { FreeMem(screen_mem, buf_size * 2); CloseLibrary((struct Library*)DOSBase); CloseLibrary((struct Library*)GfxBase); Exit(0); }
    InitTilemapBG(bg_buf);
    // Copy first 256 rows to end for seamless wrap scrolling
    for (int bpl = 0; bpl < BG_BPL; bpl++) {
        UBYTE* src = bg_buf + bg_plane_offs[bpl] * BG_PLANE_BYTES;
        UBYTE* dst = src + BG_MAP_ROWS * BG_TILE_H * ROW_BYTES;
        for (int row = 0; row < SCREEN_H; row++) {
            for (int b = 0; b < ROW_BYTES; b++)
                dst[row * ROW_BYTES + b] = src[row * ROW_BYTES + b];
        }
    }
    // (walls removed — 320px tilemap covers full screen)

    // --- Allocate HW sprite chip RAM (4 attached pairs × 2 channels × 12 words) ---
    {
        const ULONG sprDataSize = SPR_MAX_PAIRS * 2 * SPR_CHAN_WORDS * sizeof(UWORD);
        g_SprDataA = (UWORD*)AllocMem(sprDataSize, MEMF_CHIP | MEMF_CLEAR);
        g_SprDataB = (UWORD*)AllocMem(sprDataSize, MEMF_CHIP | MEMF_CLEAR);
        if (!g_SprDataA || !g_SprDataB) {
            FreeMem(screen_mem, buf_size * 2);
            if (g_SprDataA) FreeMem(g_SprDataA, sprDataSize);
            if (g_SprDataB) FreeMem(g_SprDataB, sprDataSize);
            CloseLibrary((struct Library*)DOSBase);
            CloseLibrary((struct Library*)GfxBase);
            Exit(0);
        }
        g_SprDataActive = g_SprDataA;
        g_SprDataBuild = g_SprDataB;
        g_SprDataPending = 0;
        for (int p = 0; p < SPR_MAX_PAIRS; p++) {
            g_SprPairShotActive[p] = -1;
            g_SprPairShotBuild[p] = -1;
        }
    }

    // --- Allocate double-buffered copper lists ---
    USHORT* copper1 = (USHORT*)AllocMem(1024, MEMF_CHIP | MEMF_CLEAR);
    USHORT* copper2 = (USHORT*)AllocMem(1024, MEMF_CHIP | MEMF_CLEAR);
    if (!copper1 || !copper2) { FreeMem(screen_mem, buf_size * 2); CloseLibrary((struct Library*)DOSBase); CloseLibrary((struct Library*)GfxBase); Exit(0); }
    USHORT* cop_show  = copper1;  // currently displayed by Copper
    USHORT* cop_build = copper2;  // being built by CPU

    TakeSystem();
    WaitVbl();

    // Build initial copper list: PF1 from bg_buf (scroll=0), PF2 from show_buf
    {
        const UBYTE* pf1[3] = { bg_buf + 0*BG_PLANE_BYTES, bg_buf + 1*BG_PLANE_BYTES, bg_buf + 2*BG_PLANE_BYTES };
        const UBYTE* pf2[3] = { show_buf + 1*plane_size, show_buf + 3*plane_size, show_buf + 5*plane_size };
        BuildCopperListEx(cop_show, pf1, pf2, 0);
    }
    custom->cop1lc = (ULONG)cop_show;
    custom->dmacon = DMAF_BLITTER;
    custom->copjmp1 = 0x7fff;
    custom->dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER | DMAF_BLITTER | DMAF_SPRITE;

    // Init sprite pointers (VBlank handler will update per frame)
    if (g_SprDataActive) {
        for (int s = 0; s < 8; s++) {
            volatile USHORT* ptr = (volatile USHORT*)(0xDFF120 + s*4);
            ULONG addr = (ULONG)(g_SprDataActive + s * SPR_CHAN_WORDS);
            ptr[0] = (USHORT)(addr >> 16);
            ptr[1] = (USHORT)addr;
            volatile USHORT* pos = (volatile USHORT*)(0xDFF140 + s*8);
            pos[0] = ((USHORT)(300 & 0xFF) << 8);  // VSTART[7:0]=44
            pos[1] = (1 << 7);                       // VSTART[8]=1 → VSTART=300
        }
    }

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

        // Render next frame (copper is showing show_buf, NOT draw_buf)
        RenderFrame(draw_buf);

        // Build copper: PF1 from bg_buf (with scroll), PF2 from draw_buf
        {
            const UBYTE* pf1[3] = { bg_buf + 0*BG_PLANE_BYTES, bg_buf + 1*BG_PLANE_BYTES, bg_buf + 2*BG_PLANE_BYTES };
            const UBYTE* pf2[3] = { draw_buf + 1*plane_size, draw_buf + 3*plane_size, draw_buf + 5*plane_size };
            BuildCopperListEx(cop_build, pf1, pf2, g_BGScrollY);
        }
        // Schedule copper swap at next VBlank
        { USHORT* tmp = cop_build; cop_build = cop_show; cop_show = tmp; }
        g_PendingCop = cop_show;

        // Swap buffers for next frame: the freshly rendered draw_buf becomes
        // show_buf, the old show_buf becomes the next render target
        { UBYTE* tmp = draw_buf; draw_buf = show_buf; show_buf = tmp; }

        // --- Game logic ---
        UBYTE joy = ReadJoy();

        // --- Advance background scroll (wraps for infinite loop) ---
        g_BGScrollY -= 2;
        if (g_BGScrollY < 0) g_BGScrollY = BG_MAP_ROWS * BG_TILE_H - 1;
        g_BorderScrollY -= 3;
        if (g_BorderScrollY < 0) g_BorderScrollY += BORDER_H;
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

                // Fire on press edge + hold to toggle polarity (stays on release)
                {
                    short fireNow = (joy & JOY_FIRE) ? 1 : 0;
                    if (fireNow && !g_FireWasPressed) {
                        // Edge press: fire once
                        g_FireHoldFrames = 0;
                        if (g_FireCooldown == 0) {
                            short sx1 = (short)(g_ShipX + 2);
                            short sx2 = (short)(g_ShipX + SHIP_W - SHOT_W - 2);
                            short sy  = (short)(g_ShipY - SHOT_H);
                            short var = (short)g_ShipPolarity;
                            short n   = 0;
                            for (int i = 0; i < MAX_SHOTS && n < 2; i++) {
                                if (!g_Shots[i].active) {
                                    g_Shots[i].active  = 1;
                                    g_Shots[i].variant = var;
                                    g_Shots[i].x = (n == 0) ? sx1 : sx2;
                                    g_Shots[i].y = sy;
                                    n++;
                                }
                            }
                            if (n > 0) {
                                short cd = (short)(FIRE_COOLDOWN - g_AbsorbCount);
                                if (cd < 0) cd = 0;
                                g_FireCooldown = cd;
                                if (g_AbsorbCount > 0) g_AbsorbCount--;
                            }
                        }
                    }
                    if (fireNow) {
                        g_FireHoldFrames++;
                        // Hold to toggle polarity (once per hold)
                        if (g_FireHoldFrames == POLARITY_HOLD_FRAMES) {
                            short newPol = g_ShipPolarity ? 0 : 1;
                            g_ForceFieldTransition = 4;
                            g_ForceFieldSweepDir   = newPol ? 0 : 1;
                            g_ShipPolarity = newPol;
                        }
                    } else {
                        g_FireHoldFrames = 0;
                    }
                    g_FireWasPressed = fireNow;
                    if (g_FireCooldown > 0) g_FireCooldown--;
                    if (g_ForceFieldTransition > 0) g_ForceFieldTransition--;
                }

                if (CheckBorderCollision(g_ShipX, g_ShipY, g_BorderScrollY)) {
                    SpawnExplosion(g_ShipX, g_ShipY, EXP_KIND_SHIP);
                    g_ShipExploding  = 1;
                    g_ShipExplTimer  = SHIP_EXPL_TIMER;
                    g_Lives--;
                }

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
                    }
                }
            }

            // --- Update player shots (68k ASM) ---
            AsmUpdatePlayerShots();

            // --- Update enemies (68k ASM: just movement + off-screen) ---
            AsmUpdateEnemies();

            // Simple shot-enemy collision
            for (int i = 0; i < MAX_ENEMIES; i++) {
                TEnemy* e = &g_Enemies[i];
                if (!e->active) continue;

                // Simple shot-enemy collision
                for (int s = 0; s < MAX_SHOTS; s++) {
                    if (!g_Shots[s].active) continue;
                    if (g_Shots[s].x + SHOT_W  > e->x &&
                        g_Shots[s].x            < e->x + ENEMY_W &&
                        g_Shots[s].y + SHOT_H  > e->y &&
                        g_Shots[s].y            < e->y + ENEMY_H) {
                        g_Shots[s].active = 0;
                        e->health--;
                        if (e->health <= 0) {
                            e->active = 0;
                            g_WaveKilled++;
                            switch (e->type) {
                                case ENEMY_TYPE_BASIC:  g_Score += ENEMY_SCORE_BASIC;  break;
                                case ENEMY_TYPE_FAST:   g_Score += ENEMY_SCORE_FAST;   break;
                            }
                            SpawnExplosion(e->x, e->y, EXP_KIND_ENEMY);
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
                    if (e->x + ENEMY_W  > g_ShipX + SHIP_HIT_OX &&
                        e->x            < g_ShipX + SHIP_HIT_OX + SHIP_HIT_W &&
                        e->y + ENEMY_H  > g_ShipY + SHIP_HIT_OY &&
                        e->y            < g_ShipY + SHIP_HIT_OY + SHIP_HIT_H) {
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

            // --- Update enemy shots (68k ASM: just movement + off-screen) ---
            AsmUpdateEnemyShots();

            // Enemy shot vs ship / dome interaction:
            //  1. Same polarity AND within ABSORB_RADIUS of ship center → ABSORB
            //  2. Opposite polarity AND overlapping ship hitbox        → DAMAGE
            for (int i = 0; i < MAX_ENEMY_SHOTS; i++) {
                if (!g_EnemyShots[i].active) continue;
                if (g_ShipExploding) continue;

                short sx = g_EnemyShots[i].x;
                short sy = g_EnemyShots[i].y;

                // Absorption check (same polarity only)
                if (g_EnemyShots[i].variant == g_ShipPolarity) {
                    short shipCX = (short)(g_ShipX + SHIP_W/2);
                    short shipCY = (short)(g_ShipY + SHIP_H/2);
                    short dx = (short)((sx + ENEMYSHOT_W/2) - shipCX);
                    short dy = (short)((sy + ENEMYSHOT_H/2) - shipCY);
                    long dist2 = (long)dx*dx + (long)dy*dy;
                    if (dist2 <= (long)ABSORB_RADIUS * (long)ABSORB_RADIUS) {
                        AbsorbEnemyShot(i);
                        continue;
                    }
                }

                // Damage check (opposite polarity only, AABB on ship hitbox)
                if (g_EnemyShots[i].variant != g_ShipPolarity) {
                    if (sx + ENEMYSHOT_W > g_ShipX + SHIP_HIT_OX &&
                        sx               < g_ShipX + SHIP_HIT_OX + SHIP_HIT_W &&
                        sy + ENEMYSHOT_H > g_ShipY + SHIP_HIT_OY &&
                        sy               < g_ShipY + SHIP_HIT_OY + SHIP_HIT_H) {
                        g_EnemyShots[i].active = 0;
                        SpawnExplosion(g_ShipX, g_ShipY, EXP_KIND_SHIP);
                        g_ShipExploding  = 1;
                        g_ShipExplTimer  = SHIP_EXPL_TIMER;
                        g_Lives--;
                    }
                }
            }

            // --- Enemy firing (68k ASM) ---
            AsmEnemyFire();

            // --- Update explosions (68k ASM) ---
            AsmUpdateExplosions();

            // Check game over
            if (g_GameState == GS_PLAYING && g_Level > ENDGAME_FINAL_LEVEL)
                g_GameState = GS_WIN;

            // --- Palette: TOT FIX (mai es canvia) ---
            //   La polaritat es gestiona renderitzant a plans diferents,
            //   no canviant la paleta.
            if (g_ShipExploding) {
                g_Palette[12] = 0x000;  // dome off during explosion
            } else {
                g_Palette[12] = 0x0CF;  // restore blue for normal gameplay
            }

        } else if (g_GameState == GS_GAMEOVER || g_GameState == GS_WIN) {
            if (joy & JOY_FIRE) {
                g_GameState = GS_TITLE;
                ResetGameSession();
            }
        }

    }

    // ========================================================================
    // SHUTDOWN
    // ========================================================================
    FreeSystem();

    FreeMem(screen_mem, buf_size * 2);
    FreeMem(bg_buf, BG_PLANE_BYTES * BG_BPL);
    FreeMem(copper1, 1024);
    FreeMem(copper2, 1024);
    // (no per-frame resources to free)

    CloseLibrary((struct Library*)DOSBase);
    CloseLibrary((struct Library*)GfxBase);
}
