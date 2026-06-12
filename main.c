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

// Draw 32px bob on PF2 only (planes 1,3,5)
// colorMask: bit 0 -> plane 1, bit 1 -> plane 3, bit 2 -> plane 5
static void DrawBob32(UBYTE* screen_mem,
                      const UWORD* mask, const UWORD* data,
                      short x, short y, UBYTE colorMask) {
    if (x <= -32 || x >= SCREEN_W || y <= -24 || y >= SCREEN_H) return;
    UWORD shift = (UWORD)(x & 15);
    UWORD rows  = 24;
    const UWORD* m = mask;
    const UWORD* d = data;
    if (y < 0) { m += (UWORD)((-y)*2); d += (UWORD)((-y)*2); rows = (UWORD)(24+y); y = 0; }
    if (y + (short)rows > SCREEN_H) rows = (UWORD)(SCREEN_H - y);
    if (rows == 0) return;
    UWORD wx = (UWORD)(x < 0 ? 0 : x) >> 4;
    static const UBYTE pf2_planes[3] = { 1, 3, 5 };
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
        for (int i = 0; i < 3; i++) {
            UWORD* plane = (UWORD*)(screen_mem + pf2_planes[i] * PLANE_BYTES);
            if (colorMask & (1 << i)) {
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
        phi[base]   = (UWORD)((phi[base]   & ~mv0) | (hv0 & mv0));
        phi[base+1] = (UWORD)((phi[base+1] & ~mv1) | (hv1 & mv1));
        if (wx + 2 < ROW_BYTES / 2)
            phi[base+2] = (UWORD)((phi[base+2] & ~mv2) | (hv2 & mv2));
        // Write planeLo (PF2 bit 1)
        UWORD* plo = (UWORD*)(screen_mem + planeLo * PLANE_BYTES);
        plo[base]   = (UWORD)((plo[base]   & ~mv0) | (lv0 & mv0));
        plo[base+1] = (UWORD)((plo[base+1] & ~mv1) | (lv1 & mv1));
        if (wx + 2 < ROW_BYTES / 2)
            plo[base+2] = (UWORD)((plo[base+2] & ~mv2) | (lv2 & mv2));
        // Also clear plane 5 (PF2 bit 2) in mask area
        UWORD* pl5 = (UWORD*)(screen_mem + 5 * PLANE_BYTES);
        pl5[base]   &= ~mv0;
        pl5[base+1] &= ~mv1;
        if (wx + 2 < ROW_BYTES / 2)
            pl5[base+2] &= ~mv2;
    }
}

// Draw 16px-wide bob with 2 independent data bitplanes (PF2 only)
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
        // Also clear plane 5 (PF2 bit 2) in mask area
        UWORD* pl5 = (UWORD*)(screen_mem + 5 * PLANE_BYTES);
        pl5[base]   &= ~mv0;
        if (wx + 1 < ROW_BYTES / 2)
            pl5[base+1] &= ~mv1;
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

// Draw ship (32x24) - dual-playfield: write to planes 1,3 only
// Each polarity has its own 4-bitplane data; we pick the right one based
// on colorMode and render with the standard 2-BPL merge (no swap):
//   lo -> BPL2 (PF2 bit 0)
//   hi -> BPL4 (PF2 bit 1)
//   lo AND hi -> slot 11 (PF2 bit 0+1, light grey interior)
static void DrawShipAnim(UBYTE* screen_mem, short x, short y, UBYTE frame, UBYTE colorMode) {
    (void)frame;
    if (x <= -32 || x >= SCREEN_W || y <= -24 || y >= SCREEN_H) return;
    UWORD shift = (UWORD)(x & 15);
    UWORD rows  = SHIP_W_HEIGHT;
    UWORD skip  = 0;
    if (y < 0) { skip = (UWORD)(-y); rows = (UWORD)(SHIP_W_HEIGHT - skip); y = 0; }
    if (y + (short)rows > SCREEN_H) rows = (UWORD)(SCREEN_H - y);
    if (rows == 0) return;
    UWORD wx = (UWORD)(x < 0 ? 0 : x) >> 4;
    UWORD* pl1 = (UWORD*)(screen_mem + 1 * PLANE_BYTES);
    UWORD* pl3 = (UWORD*)(screen_mem + 3 * PLANE_BYTES);
    UWORD* pl5 = (UWORD*)(screen_mem + 5 * PLANE_BYTES);
    // Pick the polarity-specific data tables
    const UWORD* mask    = (colorMode == 0) ? SHIP_W_A_MASK    : SHIP_W_B_MASK;
    const UWORD* const* planes = (colorMode == 0) ? SHIP_W_A_PLANES : SHIP_W_B_PLANES;
    const UWORD* p0 = planes[0];
    const UWORD* p1 = planes[1];
    const UWORD* p2 = planes[2];
    const UWORD* p3 = planes[3];
    for (UWORD row = 0; row < rows; row++) {
        UWORD ri   = row + skip;
        UWORD m0   = mask[ri*2],   m1 = mask[ri*2+1];
        UWORD mv0  = m0 >> shift;
        UWORD mv1  = shift ? (UWORD)((m0 << (16-shift)) | (m1 >> shift)) : m1;
        UWORD mv2  = shift ? (UWORD)(m1 << (16-shift)) : 0;
        UWORD ry   = (UWORD)y + row;
        UWORD base = ry * (ROW_BYTES / 2) + wx;
        UWORD lo0 = (UWORD)(p0[ri*2] | p1[ri*2]);
        UWORD lo1 = (UWORD)(p0[ri*2+1] | p1[ri*2+1]);
        UWORD hi0 = (UWORD)(p2[ri*2] | p3[ri*2]);
        UWORD hi1 = (UWORD)(p2[ri*2+1] | p3[ri*2+1]);
        UWORD lv0 = lo0 >> shift;
        UWORD lv1 = shift ? (UWORD)((lo0 << (16-shift)) | (lo1 >> shift)) : lo1;
        UWORD lv2 = shift ? (UWORD)(lo1 << (16-shift)) : 0;
        UWORD hv0 = hi0 >> shift;
        UWORD hv1 = shift ? (UWORD)((hi0 << (16-shift)) | (hi1 >> shift)) : hi1;
        UWORD hv2 = shift ? (UWORD)(hi1 << (16-shift)) : 0;
        // Always: lo -> BPL2, hi -> BPL4 (no swap). Each polarity has its
        // own design encoded in the bitplanes, so the same render path works
        // for both. pl5 cleared in the ship area to avoid leftover bits.
        pl1[base]   = (UWORD)((pl1[base]   & ~mv0) | (lv0 & mv0));
        pl1[base+1] = (UWORD)((pl1[base+1] & ~mv1) | (lv1 & mv1));
        if (wx + 2 < ROW_BYTES / 2)
            pl1[base+2] = (UWORD)((pl1[base+2] & ~mv2) | (lv2 & mv2));
        pl3[base]   = (UWORD)((pl3[base]   & ~mv0) | (hv0 & mv0));
        pl3[base+1] = (UWORD)((pl3[base+1] & ~mv1) | (hv1 & mv1));
        if (wx + 2 < ROW_BYTES / 2)
            pl3[base+2] = (UWORD)((pl3[base+2] & ~mv2) | (hv2 & mv2));
        pl5[base]   &= ~mv0;
        pl5[base+1] &= ~mv1;
        if (wx + 2 < ROW_BYTES / 2)
            pl5[base+2] &= ~mv2;
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
    UWORD* plane = (UWORD*)(screen_mem + planeIdx * PLANE_BYTES);
    for (UWORD row = 0; row < rows; row++) {
        UWORD ri   = row + skip;
        UWORD m0 = mask[ri*3], m1 = mask[ri*3+1], m2 = mask[ri*3+2];
        UWORD mv0 = m0 >> shift;
        UWORD mv1 = shift ? (UWORD)((m0 << (16-shift)) | (m1 >> shift)) : m1;
        UWORD mv2 = shift ? (UWORD)((m1 << (16-shift)) | (m2 >> shift)) : m2;
        UWORD ry   = (UWORD)y + row;
        UWORD base = ry * (ROW_BYTES / 2) + wx;
        plane[base]   |= mv0;
        if (wx + 1 < ROW_BYTES / 2) plane[base+1] |= mv1;
        if (wx + 2 < ROW_BYTES / 2) plane[base+2] |= mv2;
        // 4th word: the rightmost `shift` pixels of the mask spill over
        // the 3 standard words. Without this the right edge of the bubble
        // (or the sweep) is clipped when the dome isn't word-aligned.
        if (shift > 0 && wx + 3 < ROW_BYTES / 2) {
            plane[base+3] |= m2 << (16 - shift);
        }
    }
}

// Draw the force field. Two modes:
//   - Normal  (transition == 0): scrolling banded-dither bubble in BPL6
//                                 (Sonic 1-style energy lines). Drawn BEFORE
//                                 the ship so the ship shows on top.
//   - Sweep   (transition > 0):  OPAQUE disc split by a vertical wiper,
//                                 BPL4 = source (old polarity), BPL6 = dest
//                                 (new polarity). Drawn AFTER the ship so the
//                                 dome covers the ship. The wiper sweeps from
//                                 left to right (sweepDir=0, going to black)
//                                 or right to left (sweepDir=1, going to white).
static void DrawForceField(UBYTE* screen_mem, short shipX, short shipY,
                           short pulse, short transition, short sweepDir) {
    // Mask is 48x48 centered at (24,24). Ship center is (shipX+16, shipY+12),
    // so draw the mask at (shipX-8, shipY-12).
    short mx = (short)(shipX - 8);
    short my = (short)(shipY - 12);
    (void)pulse;  // reserved for future size cycling

    if (transition > 0) {
        // Polarity sweep. transition counts down 4..1. phase = 0..3.
        short phase = 4 - transition;
        if (phase < 0) phase = 0;
        if (phase > 3) phase = 3;
        // Forward (going to black): play frames 0,1,2,3. Reverse: 3,2,1,0.
        short idx = (sweepDir == 0) ? phase : (3 - phase);
        // Forward: BPL4 = source (right of wiper), BPL6 = destination (left).
        // Reverse: swap the BPL assignments so destination still ends up on
        // the right and source on the left of the wiper.
        if (sweepDir == 0) {
            DrawForceFieldMask(screen_mem, mx, my, g_FFSweepSrcSet[idx], 3); // BPL4
            DrawForceFieldMask(screen_mem, mx, my, g_FFSweepDstSet[idx], 5); // BPL6
        } else {
            DrawForceFieldMask(screen_mem, mx, my, g_FFSweepSrcSet[idx], 5); // BPL6
            DrawForceFieldMask(screen_mem, mx, my, g_FFSweepDstSet[idx], 3); // BPL4
        }
    } else {
        // Normal scrolling bubble. Drawn before the ship.
        short scroll = (g_FrameCounter >> 3) & 3;
        DrawForceFieldMask(screen_mem, mx, my, g_FFBubScrollSet[scroll], 5);  // BPL6 only
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
    // PF2 (game sprites): slots 8-15
    0x0000,              //  8  PF2 transparent (shows PF1 behind)
    0x0FFF,              //  9  PF2 white (white enemy shots)
    0x0111,              // 10  PF2 near-black (black enemy shots)
    0x0CCC,              // 11  PF2 light grey
    0x0FF0,              // 12  PF2 yellow (player shots)
    0x0212,              // 13  PF2 border dark
    0x0756,              // 14  PF2 border mid
    0x0434,              // 15  PF2 border light
    // Slots 16-28: hardware sprites (enemy shots, 4 sprites, red outline + white fill)
    0x0000,              // 16  spare
    0x0F00,              // 17  SPR0 outline (red)
    0x0000,              // 18  SPR0 unused
    0x0FFF,              // 19  SPR0 fill (white)
    0x0F00,              // 20  SPR1 outline
    0x0000,              // 21  SPR1 unused
    0x0FFF,              // 22  SPR1 fill
    0x0F00,              // 23  SPR2 outline
    0x0000,              // 24  SPR2 unused
    0x0FFF,              // 25  SPR2 fill
    0x0F00,              // 26  SPR3 outline
    0x0000,              // 27  SPR3 unused
    0x0FFF,              // 28  SPR3 fill
    // Slots 29-31: unused
    0x0000, 0x0000, 0x0000,
};

// ============================================================================
// GAME STATE
// ============================================================================

static short g_GameState  = GS_PLAYING;
static short g_TitleMode  = TS_MENU;
static short g_CurrentBiome = 0;
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
static TStar g_Stars1[N_STARS_1];
static TStar g_Stars2[N_STARS_2];
static TStar g_Stars3[N_STARS_3];

// Hi-scores
static THiScore g_HiScores[HISCORE_COUNT];

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
    g_FireCooldown    = 0;
    g_ShipPolarity    = 0;
    g_FireHoldFrames  = 0;
}

static void ResetGameSession() {
    g_Score      = 0;
    g_Lives      = 3;
    g_Level      = 1;
    g_NextLifeAt = EXTRA_LIFE_EVERY;
    g_CurrentBiome = 0;
    g_BGScrollY     = BG_MAP_ROWS * BG_TILE_H - SCREEN_H;
    g_BorderScrollY = BORDER_H - SCREEN_H;
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

static void RenderFrame(UBYTE* screen_mem) {
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
                DrawShipAnim(screen_mem, g_ShipX, g_ShipY, animFrame, (UBYTE)g_ShipPolarity);
                DrawForceField(screen_mem, g_ShipX, g_ShipY, 0, g_ForceFieldTransition, g_ForceFieldSweepDir);
            } else {
                // Normal: scrolling bubble (BPL6 annulus) before the ship.
                short ffPulse = (short)((g_FrameCounter / 6) % 3);
                DrawForceField(screen_mem, g_ShipX, g_ShipY, ffPulse, 0, 0);
                UBYTE animFrame = (UBYTE)((g_FrameCounter >> 2) & 3);
                DrawShipAnim(screen_mem, g_ShipX, g_ShipY, animFrame, (UBYTE)g_ShipPolarity);
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
            // variant 0 = white (slot 9, colorMask 0x01), variant 1 = black (slot 10, colorMask 0x02)
            UBYTE pcm = (g_Shots[i].variant == 0) ? 0x01 : 0x02;
            DrawBob16(screen_mem, g_ShotMask, g_ShotData, g_Shots[i].x, g_Shots[i].y, pcm, 8);
        }
        for (int i = 0; i < MAX_ENEMY_SHOTS; i++) {
            if (!g_EnemyShots[i].active) continue;
            // white shot (variant=0) uses slot 9 (white), black shot (variant=1) uses slot 10 (near-black)
            UBYTE cm = (g_EnemyShots[i].variant == 0) ? 0x01 : 0x02;
            DrawBob16(screen_mem, g_EShotMask, g_EShotData,
                      g_EnemyShots[i].x, g_EnemyShots[i].y, cm, 6);
        }
        for (int i = 0; i < MAX_EXPLOSIONS; i++) {
            TExplosion* ex = &g_Explosions[i];
            if (!ex->active) continue;
            UWORD fr = (UWORD)((ex->frame >> 1) & 3);
            DrawBob16(screen_mem, g_ExpMasks[fr], g_ExpData[fr], ex->x, ex->y, 0x03, 8);
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

    // --- Hardware sprite data for enemy shots (16x8, in chip RAM) ---
    #define HWSPR_H 8
    static const UWORD g_HwSprData_src[(HWSPR_H+1)*2] = {
        0x0180,0x0000, 0x03C0,0x0180, 0x07E0,0x03C0, 0x0FF0,0x07E0,
        0x0FF0,0x07E0, 0x07E0,0x03C0, 0x03C0,0x0180, 0x0180,0x0000,
        0x0000,0x0000
    };
    UWORD* g_HwSprData = (UWORD*)AllocMem(sizeof(g_HwSprData_src), MEMF_CHIP);
    if (g_HwSprData) {
        for (int i = 0; i < (HWSPR_H+1)*2; i++)
            g_HwSprData[i] = g_HwSprData_src[i];
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

    // --- Init hardware sprite pointers (DMA 0-3, enemy shots) ---
    {
        ULONG addr = (ULONG)g_HwSprData;
        USHORT hw = (USHORT)(addr >> 16);
        USHORT lw = (USHORT)addr;
        for (int s = 0; s < 4; s++) {
            volatile USHORT* ptr = (volatile USHORT*)(0xDFF120 + s*4);
            ptr[0] = hw;
            ptr[1] = lw;
        }
    }
    for (int s = 0; s < 4; s++) {
        volatile USHORT* pos = (volatile USHORT*)(0xDFF140 + s*8);
        pos[0] = (USHORT)(256 << 8);
        pos[1] = 0;
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

                // Fire with polarity swap
                {
                    short fireHeldNow = (joy & JOY_FIRE) ? 1 : 0;
                    if (fireHeldNow) {
                        g_FireHoldFrames++;
                        // Switch to black polarity after holding for POLARITY_HOLD_FRAMES
                        if (g_FireHoldFrames == POLARITY_HOLD_FRAMES) {
                            g_ForceFieldTransition = 4;     // 4-frame sweep
                            g_ForceFieldSweepDir   = 0;     // white->black: wiper L->R
                        }
                        g_ShipPolarity = (g_FireHoldFrames >= POLARITY_HOLD_FRAMES) ? 1 : 0;
                    } else {
                        if (g_ShipPolarity == 1) {
                            g_ForceFieldTransition = 4;     // 4-frame sweep
                            g_ForceFieldSweepDir   = 1;     // black->white: wiper R->L
                        }
                        g_ShipPolarity   = 0;
                        g_FireHoldFrames = 0;
                    }
                    // Auto-fire on every cooldown cycle (existing behavior)
                    if (fireHeldNow && g_FireCooldown == 0) {
                        for (int i = 0; i < MAX_SHOTS; i++) {
                            if (!g_Shots[i].active) {
                                g_Shots[i].active  = 1;
                                g_Shots[i].variant = (short)g_ShipPolarity;
                                g_Shots[i].x = (short)(g_ShipX + SHIP_W/2 - SHOT_W/2);
                                g_Shots[i].y = (short)(g_ShipY - SHOT_H);
                                g_FireCooldown = FIRE_COOLDOWN;
                                break;
                            }
                        }
                    }
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
                        g_CurrentBiome = ((g_Level - 1) / 5) % BIOME_COUNT;
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

            // Enemy shot vs ship collision (only shots of opposite polarity damage the ship)
            for (int i = 0; i < MAX_ENEMY_SHOTS; i++) {
                if (!g_EnemyShots[i].active) continue;
                if (!g_ShipExploding && g_EnemyShots[i].variant != g_ShipPolarity) {
                    short sx = g_EnemyShots[i].x;
                    short sy = g_EnemyShots[i].y;
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

            // --- Update force field colors (BPL6 = g_Palette[12], BPL4 = g_Palette[10]) ---
            // The dome is drawn into BPL6 (annulus or sweep destination) and
            // BPL4 (sweep source). Both come from g_Palette, reloaded by the
            // Copper every frame in BuildCopperListEx. (Writing custom->color[]
            // directly would be silently overwritten by the Copper.)
            //
            // Amiga colors are 0xRGB (4 bits each):
            //   0x0CF = light blue, 0x0FF = cyan, 0xA00 = dark red, 0xF00 = red.
            if (g_ShipExploding) {
                g_Palette[10] = 0x000;
                g_Palette[12] = 0x000;
            } else if (g_ForceFieldTransition > 0) {
                // Polarity sweep: split-color dome (BPL4=source, BPL6=dest)
                if (g_ForceFieldSweepDir == 0) {
                    // white->black: blue sweeps out, red sweeps in from the left
                    g_Palette[10] = 0x0CF;  // BPL4: source = light blue
                    g_Palette[12] = 0xF00;  // BPL6: destination = red
                } else {
                    // black->white: red sweeps out, blue sweeps in from the right
                    g_Palette[10] = 0xA00;  // BPL4: source = dark red
                    g_Palette[12] = 0x0CF;  // BPL6: destination = light blue
                }
            } else {
                // Normal scrolling bubble: single color, pulsing.
                short pulse = (short)((g_FrameCounter >> 2) & 0x1F);
                g_Palette[10] = 0x0111;  // BPL4: keep player-shot near-black
                if (g_ShipPolarity == 0) {
                    // White polarity: light blue / cyan
                    g_Palette[12] = (pulse < 16) ? (USHORT)0x0CF : (USHORT)0x0FF;
                } else {
                    // Black polarity: dark red / red
                    g_Palette[12] = (pulse < 16) ? (USHORT)0xA00 : (USHORT)0xF00;
                }
            }

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
    FreeMem(bg_buf, BG_PLANE_BYTES * BG_BPL);
    FreeMem(copper1, 1024);
    FreeMem(copper2, 1024);
    // (no per-frame resources to free)

    CloseLibrary((struct Library*)DOSBase);
    CloseLibrary((struct Library*)GfxBase);
}
