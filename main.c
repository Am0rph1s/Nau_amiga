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

// Pre-render tilemap background into bg_buf (called once at startup)
#define BG_PLANE_BYTES (ROW_BYTES * (BG_MAP_ROWS * BG_TILE_H + SCREEN_H))
static const int bg_plane_offs[BG_BPL] = { 0, 2, 4 };

static void InitTilemapBG(UBYTE* bg_buf) {
    for (int map_row = 0; map_row < BG_MAP_ROWS; map_row++) {
        for (int tx = 0; tx < BG_MAP_COLS; tx++) {
            UWORD tile_idx = bg_tilemap[map_row * BG_MAP_COLS + tx];
            const UBYTE* tile = bg_tiles[tile_idx];
            int scr_x = GAME_X0 + tx * BG_TILE_W;

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

// Draw white ship (32x24) - dual-playfield: write to planes 1,3 only
// Merge original 4 bitplanes: PF2 bit0 (plane1) = bpl0|bpl1, PF2 bit1 (plane3) = bpl2|bpl3
static void DrawShipAnim(UBYTE* screen_mem, short x, short y, UBYTE frame) {
    (void)frame;
    if (x <= -32 || x >= SCREEN_W || y <= -24 || y >= SCREEN_H) return;
    UWORD shift = (UWORD)(x & 15);
    UWORD rows  = SHIP_W_HEIGHT;
    UWORD skip  = 0;
    if (y < 0) { skip = (UWORD)(-y); rows = (UWORD)(SHIP_W_HEIGHT - skip); y = 0; }
    if (y + (short)rows > SCREEN_H) rows = (UWORD)(SCREEN_H - y);
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
        // Merge planes 0|1 -> PF2 bit0 (written to plane 1)
        UWORD lo0 = (UWORD)(SHIP_W_PLANES[0][ri*2] | SHIP_W_PLANES[1][ri*2]);
        UWORD lo1 = (UWORD)(SHIP_W_PLANES[0][ri*2+1] | SHIP_W_PLANES[1][ri*2+1]);
        // Merge planes 2|3 -> PF2 bit1 (written to plane 3)
        UWORD hi0 = (UWORD)(SHIP_W_PLANES[2][ri*2] | SHIP_W_PLANES[3][ri*2]);
        UWORD hi1 = (UWORD)(SHIP_W_PLANES[2][ri*2+1] | SHIP_W_PLANES[3][ri*2+1]);
        // Shift lo
        UWORD lv0 = lo0 >> shift;
        UWORD lv1 = shift ? (UWORD)((lo0 << (16-shift)) | (lo1 >> shift)) : lo1;
        UWORD lv2 = shift ? (UWORD)(lo1 << (16-shift)) : 0;
        // Shift hi
        UWORD hv0 = hi0 >> shift;
        UWORD hv1 = shift ? (UWORD)((hi0 << (16-shift)) | (hi1 >> shift)) : hi1;
        UWORD hv2 = shift ? (UWORD)(hi1 << (16-shift)) : 0;
        // Write plane 1 (PF2 bit0)
        UWORD* pl1 = (UWORD*)(screen_mem + 1 * PLANE_BYTES);
        pl1[base]   = (UWORD)((pl1[base]   & ~mv0) | (lv0 & mv0));
        pl1[base+1] = (UWORD)((pl1[base+1] & ~mv1) | (lv1 & mv1));
        if (wx + 2 < ROW_BYTES / 2)
            pl1[base+2] = (UWORD)((pl1[base+2] & ~mv2) | (lv2 & mv2));
        // Write plane 3 (PF2 bit1)
        UWORD* pl3 = (UWORD*)(screen_mem + 3 * PLANE_BYTES);
        pl3[base]   = (UWORD)((pl3[base]   & ~mv0) | (hv0 & mv0));
        pl3[base+1] = (UWORD)((pl3[base+1] & ~mv1) | (hv1 & mv1));
        if (wx + 2 < ROW_BYTES / 2)
            pl3[base+2] = (UWORD)((pl3[base+2] & ~mv2) | (hv2 & mv2));
        // Clear plane 5 (PF2 bit2) in ship mask area
        UWORD* pl5 = (UWORD*)(screen_mem + 5 * PLANE_BYTES);
        pl5[base]   &= ~mv0;
        pl5[base+1] &= ~mv1;
        if (wx + 2 < ROW_BYTES / 2)
            pl5[base+2] &= ~mv2;
    }
}

static void RenderFrame(UBYTE* screen_mem); // defined after globals

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
    0x0444,              //  9  PF2 dark grey
    0x0888,              // 10  PF2 mid grey
    0x0CCC,              // 11  PF2 light grey
    0x0FF0,              // 12  PF2 yellow (shots)
    0x0F60,              // 13  PF2 orange (explosions)
    0x0F00,              // 14  PF2 red (boss)
    0x0FFF,              // 15  PF2 white (boss)
    // Slots 16-31: unused
    0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000,
};

// ============================================================================
// GAME STATE
// ============================================================================

static volatile short g_FrameCounter = 0;
static short g_GameState  = GS_PLAYING;
static short g_TitleMode  = TS_MENU;
static short g_CurrentBiome = 0;
static short g_StarsEnabled = 0;  // 0=planet mode (no stars), 1=space mode
static short g_BGScrollY  = 0;    // tilemap vertical scroll offset

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
    g_BGScrollY   = BG_MAP_ROWS * BG_TILE_H - SCREEN_H;
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


static void BuildCopperListEx(USHORT* cop, const UBYTE** pf1_planes, const UBYTE** pf2_planes, short scroll_y) {
    const USHORT x     = 129;
    const USHORT width = 320;
    const USHORT height= 256;
    const USHORT y     = 44;
    const USHORT RES   = 8;
    USHORT xstop = x + width;
    USHORT ystop = y + height;
    USHORT fw    = (x >> 1) - RES;

    cop = copSetReg(cop, offsetof(struct Custom, ddfstrt),  fw);
    cop = copSetReg(cop, offsetof(struct Custom, ddfstop),  fw + (((width>>4)-1)<<3));
    cop = copSetReg(cop, offsetof(struct Custom, diwstrt),  x + (y<<8));
    cop = copSetReg(cop, offsetof(struct Custom, diwstop),  (xstop-256) + ((ystop-256)<<8));

    cop = copSetReg(cop, offsetof(struct Custom, bplcon0), (1<<9) | (6<<12) | (1<<10));
    cop = copSetReg(cop, offsetof(struct Custom, bplcon1), 0);
    cop = copSetReg(cop, offsetof(struct Custom, bplcon2), (1<<6) | (2<<2) | (1<<1));
    cop = copSetReg(cop, offsetof(struct Custom, bpl1mod), 0);
    cop = copSetReg(cop, offsetof(struct Custom, bpl2mod), 0);

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
    if (g_StarsEnabled) {
        for (int i = 0; i < N_STARS_1; i++) DrawPixel(screen_mem, g_Stars1[i].x, g_Stars1[i].y, 1);
        for (int i = 0; i < N_STARS_2; i++) DrawPixel(screen_mem, g_Stars2[i].x, g_Stars2[i].y, 2);
        for (int i = 0; i < N_STARS_3; i++) DrawPixel(screen_mem, g_Stars3[i].x, g_Stars3[i].y, 3);
    }
    if (g_GameState == GS_PLAYING || g_GameState == GS_GAMEOVER) {
        if (!g_ShipExploding) {
            UBYTE animFrame = (UBYTE)((g_FrameCounter >> 2) & 3);
            DrawShipAnim(screen_mem, g_ShipX, g_ShipY, animFrame);
        }
        for (int i = 0; i < MAX_ENEMIES; i++) {
            TEnemy* e = &g_Enemies[i];
            if (!e->active) continue;
            if (e->type == ENEMY_TYPE_BASIC) {
                DrawBob32_2bpl(screen_mem, g_EnemyBasic24Mask,
                               g_EnemyBasic24Hi, g_EnemyBasic24Lo,
                               e->x, e->y, 1, 3);
            } else if (e->type == ENEMY_TYPE_FAST) {
                DrawBob16_2bpl(screen_mem, g_EnemyFast16Mask,
                               g_EnemyFast16Hi, g_EnemyFast16Lo,
                               e->x, e->y, 1, 3, 16);
            } else {
                DrawBob16(screen_mem, g_EnemyMasks[e->type], g_EnemyDatas[e->type],
                          e->x, e->y, g_EnemyColor[e->type], g_EnemyRows[e->type]);
            }
        }
        for (int i = 0; i < MAX_SHOTS; i++) {
            if (!g_Shots[i].active) continue;
            DrawBob16(screen_mem, g_ShotMask, g_ShotData, g_Shots[i].x, g_Shots[i].y, 0x04, 8);
        }
        for (int i = 0; i < MAX_ENEMY_SHOTS; i++) {
            if (!g_EnemyShots[i].active) continue;
            DrawBob16(screen_mem, g_EShotMask, g_EShotData,
                      g_EnemyShots[i].x, g_EnemyShots[i].y, 0x03, 8);
        }
        for (int i = 0; i < MAX_EXPLOSIONS; i++) {
            TExplosion* ex = &g_Explosions[i];
            if (!ex->active) continue;
            UWORD fr = (UWORD)((ex->frame >> 1) & 3);
            DrawBob16(screen_mem, g_ExpMasks[fr], g_ExpData[fr], ex->x, ex->y, 0x05, 8);
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

    // Load tilemap palette into PF1 slots 0-7
    for (int i = 0; i < BG_PAL_AMIGA_COUNT && i < 8; i++)
        g_Palette[i] = bg_pal_amiga[i];
    // Override slots 0,7 with wall colors (both free — tilemap uses indices 1-6)
    g_Palette[0] = 0x0212;  // dark rock
    g_Palette[7] = 0x0753;  // light sand

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
    // Apply rocky wall texture to wall area (plane 0 = tile pattern, planes 2,4 = 0xFF)
    ParallaxInit();
    {
        UBYTE* p0 = bg_buf + 0 * BG_PLANE_BYTES;
        UBYTE* p2 = bg_buf + 2 * BG_PLANE_BYTES;
        UBYTE* p4 = bg_buf + 4 * BG_PLANE_BYTES;
        int total_rows = BG_MAP_ROWS * BG_TILE_H;
        for (int row = 0; row < total_rows; row++) {
            short t = (short)((row) & (PAR_TILE_H - 1));
            UWORD solid = g_TileSolid[t];
            UWORD deco  = g_TileDeco[t];
            UBYTE* r0 = p0 + row * ROW_BYTES;
            UBYTE* r2 = p2 + row * ROW_BYTES;
            UBYTE* r4 = p4 + row * ROW_BYTES;
            r0[0] = (UBYTE)(deco >> 8);
            r0[1] = (UBYTE)(solid & 0xFF);
            r0[34] = (UBYTE)(deco & 0xFF);
            r0[35] = (UBYTE)(solid >> 8);
            // Planes 2,4: copy plane0 pattern → color indices 0 and 7
            r2[0] = r0[0]; r2[1] = r0[1]; r2[34] = r0[34]; r2[35] = r0[35];
            r4[0] = r0[0]; r4[1] = r0[1]; r4[34] = r0[34]; r4[35] = r0[35];
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
        g_BGScrollY--;
        if (g_BGScrollY < 0) g_BGScrollY = BG_MAP_ROWS * BG_TILE_H - 1;
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
                e->y += e->vy;
                if (e->y > GAME_H) { e->active = 0; g_WaveKilled++; }

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
                                case ENEMY_TYPE_HEAVY:  g_Score += ENEMY_SCORE_HEAVY;  break;
                                case ENEMY_TYPE_DIVER:  g_Score += ENEMY_SCORE_DIVER;  break;
                                case ENEMY_TYPE_BOMBER: g_Score += ENEMY_SCORE_BOMBER; break;
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
    FreeMem(bg_buf, BG_PLANE_BYTES * BG_BPL);
    FreeMem(copper1, 1024);
    FreeMem(copper2, 1024);

    CloseLibrary((struct Library*)DOSBase);
    CloseLibrary((struct Library*)GfxBase);
}
