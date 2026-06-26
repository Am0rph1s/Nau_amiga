#pragma once
// ============================================================================
// NAU DX AMIGA - Sprite/Bob graphics data
// (Enemy sprite data lives in gfx_enemy_basic24.h / gfx_enemy_fast16.h
//  since each enemy uses 2 bitplanes + an inverted polarity variant.)
// ============================================================================



// --- PLAYER SHOT (4x16, 2-tone, split per polarity, 1 MB only) --------------
// Frame 0 (even frames):  dataHi→BPL2(white  ), dataLo→BPL6(blue   )  → blue glow  + white core
// Frame 1 (odd frames):   dataHi→BPL6(blue   ), dataLo→BPL2(white   )  → white glow + blue core
// (Just swap the data pointer; plane assignments stay fixed.)
// Black polarity: body→BPL2+OrBPL4(red), accent→BPL4(black) — static, no alternation
static CHIP_RAM_DATA UWORD g_Shot16W_Mask[16]   = { 0x6000,0xF000,0xF000,0xF000,0xF000,0xF000,0xF000,0xF000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000 };
static CHIP_RAM_DATA UWORD g_Shot16W_Accent[16] = { 0x0000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000 };
static CHIP_RAM_DATA UWORD g_Shot16W_Body[16]   = { 0x6000,0x9000,0x9000,0x9000,0x9000,0x9000,0x9000,0x9000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000 };

static CHIP_RAM_DATA UWORD g_Shot16B_Mask[16]   = { 0x6000,0xF000,0xF000,0xF000,0xF000,0xF000,0xF000,0xF000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000 };
static CHIP_RAM_DATA UWORD g_Shot16B_Body[16]   = { 0x6000,0x9000,0x9000,0x9000,0x9000,0x9000,0x9000,0x9000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000 };
static CHIP_RAM_DATA UWORD g_Shot16B_Accent[16] = { 0x0000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000 };

// --- ENEMY SHOT (10x4 diamond, 2-tone, split per polarity) ------------------
// Same colour scheme as player shots.
static CHIP_RAM_DATA UWORD g_EShotW_Mask[8]   = { 0x0,0x3c0,0x7e0,0x3c0,0x0,0x0,0x0,0x0 };
static CHIP_RAM_DATA UWORD g_EShotW_DataHi[8] = { 0x0,0x0,0x180,0x0,0x0,0x0,0x0,0x0 };
static CHIP_RAM_DATA UWORD g_EShotW_DataLo[8] = { 0x0,0x3c0,0x660,0x3c0,0x0,0x0,0x0,0x0 };

static CHIP_RAM_DATA UWORD g_EShotB_Mask[8]   = { 0x0,0x3c0,0x7e0,0x3c0,0x0,0x0,0x0,0x0 };
static CHIP_RAM_DATA UWORD g_EShotB_DataHi[8] = { 0x0,0x0,0x180,0x0,0x0,0x0,0x0,0x0 };
static CHIP_RAM_DATA UWORD g_EShotB_DataLo[8] = { 0x0,0x3c0,0x660,0x3c0,0x0,0x0,0x0,0x0 };

// --- PLAYER SHOT SPRITE DATA (8 rows, for HW sprite multiplexing) -----------
static const UWORD g_ShotSpr_Body[8]  = { 0x6000,0x9000,0x9000,0x9000,0x9000,0x9000,0x9000,0x9000 };
static const UWORD g_ShotSpr_Accent[8] = { 0x0000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000,0x6000 };

// --- EXPLOSION frames (16x16) -----------------------------------------------
static const UWORD g_Exp0Mask[16] = {
    0x0000,0x0000,0x0000,0x03C0, 0x03C0,0x0000,0x0000,0x0000,
    0x0000,0x0000,0x0000,0x0000, 0x0000,0x0000,0x0000,0x0000,
};
static const UWORD g_Exp0Data[16] = {
    0x0000,0x0000,0x0000,0x03C0, 0x03C0,0x0000,0x0000,0x0000,
    0x0000,0x0000,0x0000,0x0000, 0x0000,0x0000,0x0000,0x0000,
};
static const UWORD g_Exp1Mask[16] = {
    0x0000,0x0000,0x07E0,0x0FF0, 0x1FF8,0x1FF8,0x0FF0,0x07E0,
    0x0000,0x0000,0x0000,0x0000, 0x0000,0x0000,0x0000,0x0000,
};
static const UWORD g_Exp1Data[16] = {
    0x0000,0x0000,0x07E0,0x0FF0, 0x1FF8,0x1FF8,0x0FF0,0x07E0,
    0x0000,0x0000,0x0000,0x0000, 0x0000,0x0000,0x0000,0x0000,
};
static const UWORD g_Exp2Mask[16] = {
    0x03C0,0x0FF0,0x1FF8,0x3FFC, 0x7FFE,0x7FFE,0x7FFE,0xFFFF,
    0xFFFF,0x7FFE,0x7FFE,0x7FFE, 0x3FFC,0x1FF8,0x0FF0,0x03C0,
};
static const UWORD g_Exp2Data[16] = {
    0x03C0,0x0FF0,0x1FF8,0x3FFC, 0x7FFE,0x7FFE,0x7FFE,0xFFFF,
    0xFFFF,0x7FFE,0x7FFE,0x7FFE, 0x3FFC,0x1FF8,0x0FF0,0x03C0,
};
static const UWORD g_Exp3Mask[16] = {
    0x03C0,0x1218,0x2814,0x500A, 0x900A,0xA005,0x4003,0x3FFC,
    0x3FFC,0x4002,0xA005,0x900A, 0x500A,0x2814,0x1218,0x03C0,
};
static const UWORD g_Exp3Data[16] = {
    0x03C0,0x1218,0x2814,0x500A, 0x900A,0xA005,0x4003,0x3FFC,
    0x3FFC,0x4002,0xA005,0x900A, 0x500A,0x2814,0x1218,0x03C0,
};

// Lookup tables for drawing
static const UWORD* const g_ExpMasks[4] = { g_Exp0Mask, g_Exp1Mask, g_Exp2Mask, g_Exp3Mask };
static const UWORD* const g_ExpData[4]  = { g_Exp0Data, g_Exp1Data, g_Exp2Data, g_Exp3Data };

// --- LASER BEAM (16px wide, 256-row arrays for blitter BOB DrawBob16_2bpl) ---
// Polarity A (white/blue): outer glow 0x3FFC (14px), inner core 0x0FF0 (12px)
//   planeHi=1 (BPL2, white body), planeLo=5 (BPL6, blue accent)
// Polarity B (black/red):  outer glow 0x3FFC (14px), inner core 0x0FF0 (12px)
//   planeHi=1 (BPL2, dark body), planeLo=3 (BPL4, red accent)
// 256 rows ensures the C fallback (if ASM fails) never reads out-of-bounds.
// All rows are identical; blitter BLTAMOD=-2 re-reads the same word anyway.
// Call InitLaserGfx() once at startup to fill the arrays.
static CHIP_RAM_DATA UWORD g_LaserMask[320];
static CHIP_RAM_DATA UWORD g_LaserBodyA[320];
static CHIP_RAM_DATA UWORD g_LaserAccentA[320];
static CHIP_RAM_DATA UWORD g_LaserBodyB[320];
static CHIP_RAM_DATA UWORD g_LaserAccentB[320];

static void InitLaserGfx(void) {
    static const signed char sin_table[16] = {
        0, 1, 2, 3, 3, 2, 1, 0, -1, -2, -3, -3, -2, -1, 0, 0
    };
    for (int r = 0; r < 320; r++) {
        int idx1 = r % 16;
        int idx2 = (r + 8) % 16;
        
        int shift1 = sin_table[idx1];
        int shift2 = sin_table[idx2];
        
        // Base strand shape: 0x03F0 (6 pixels wide: bits 4 to 9)
        UWORD strand1 = 0x03F0;
        if (shift1 > 0) strand1 >>= shift1;
        else if (shift1 < 0) strand1 <<= -shift1;
        
        UWORD strand2 = 0x03F0;
        if (shift2 > 0) strand2 >>= shift2;
        else if (shift2 < 0) strand2 <<= -shift2;
        
        g_LaserMask[r] = strand1 | strand2;
        g_LaserBodyA[r]   = strand1;
        g_LaserAccentA[r] = strand2;
        g_LaserBodyB[r]   = strand1;
        g_LaserAccentB[r] = strand2;
    }
}

// Same-polarity laser collision splash animations (32x12, 2 frames)
static CHIP_RAM_DATA UWORD g_LaserSplash0_Mask[24] = {
    0x0000,0x0000, 0x0018,0x1800, 0x007E,0x7E00, 0x01E7,0xE780,
    0x07C3,0xC3E0, 0x1F00,0x00F8, 0x7C00,0x003E, 0xF000,0x000F,
    0xC000,0x0003, 0x8000,0x0001, 0x0000,0x0000, 0x0000,0x0000
};
static CHIP_RAM_DATA UWORD g_LaserSplash0_Body[24] = {
    0x0000,0x0000, 0x0000,0x0000, 0x0018,0x1800, 0x0066,0x6600,
    0x0180,0x0180, 0x0000,0x0000, 0x0000,0x0000, 0x0000,0x0000,
    0x0000,0x0000, 0x0000,0x0000, 0x0000,0x0000, 0x0000,0x0000
};
static CHIP_RAM_DATA UWORD g_LaserSplash0_Accent[24] = {
    0x0000,0x0000, 0x0018,0x1800, 0x007E,0x7E00, 0x01E7,0xE780,
    0x07C3,0xC3E0, 0x1F00,0x00F8, 0x7C00,0x003E, 0xF000,0x000F,
    0xC000,0x0003, 0x8000,0x0001, 0x0000,0x0000, 0x0000,0x0000
};

static CHIP_RAM_DATA UWORD g_LaserSplash1_Mask[24] = {
    0x0000,0x0000, 0x0000,0x0000, 0x003C,0x3C00, 0x01C3,0xC380,
    0x0780,0x01E0, 0x1E00,0x0078, 0x3C00,0x003C, 0xF000,0x000F,
    0x8000,0x0001, 0x0000,0x0000, 0x0000,0x0000, 0x0000,0x0000
};
static CHIP_RAM_DATA UWORD g_LaserSplash1_Body[24] = {
    0x0000,0x0000, 0x0000,0x0000, 0x0000,0x0000, 0x0081,0x8100,
    0x0180,0x0180, 0x0000,0x0000, 0x0000,0x0000, 0x0000,0x0000,
    0x0000,0x0000, 0x0000,0x0000, 0x0000,0x0000, 0x0000,0x0000
};
static CHIP_RAM_DATA UWORD g_LaserSplash1_Accent[24] = {
    0x0000,0x0000, 0x0000,0x0000, 0x003C,0x3C00, 0x01C3,0xC380,
    0x0780,0x01E0, 0x1E00,0x0078, 0x3C00,0x003C, 0xF000,0x000F,
    0x8000,0x0001, 0x0000,0x0000, 0x0000,0x0000, 0x0000,0x0000
};

// --- HOMING BULLET GRAPHICS (16x16, oriented with trail opposite to movement) ---

// 0. Moving Up (trail pointing straight Down)
static CHIP_RAM_DATA UWORD g_HomingShotUp_Mask[16] = {
    0x0F00, // Row 0: ball (width 8)
    0x1F80, // Row 1
    0x3FC0, // Row 2
    0x3FC0, // Row 3
    0x3FC0, // Row 4
    0x1F80, // Row 5
    0x0F00, // Row 6
    0x0BD0, // Row 7: transition
    0x05A0, // Row 8: trail start
    0x0240, // Row 9
    0x03C0, // Row 10
    0x0180, // Row 11
    0x0180, // Row 12: trail end
    0x0000,
    0x0000,
    0x0000
};
static CHIP_RAM_DATA UWORD g_HomingShotUp_Body[16] = {
    0x0F00, // Row 0: ball
    0x1F80, // Row 1
    0x3FC0, // Row 2
    0x3FC0, // Row 3
    0x3FC0, // Row 4
    0x1F80, // Row 5
    0x0F00, // Row 6
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000
};
static CHIP_RAM_DATA UWORD g_HomingShotUp_Accent[16] = {
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0BD0, // Row 7: transition
    0x05A0, // Row 8: trail start
    0x0240, // Row 9
    0x03C0, // Row 10
    0x0180, // Row 11
    0x0180, // Row 12: trail end
    0x0000,
    0x0000,
    0x0000
};

// 1. Moving Down (trail pointing straight Up)
static CHIP_RAM_DATA UWORD g_HomingShotDn_Mask[16] = {
    0x0180, // Row 0:    **
    0x0180, // Row 1:    **
    0x03C0, // Row 2:   ****
    0x0240, // Row 3:  *    *
    0x05A0, // Row 4: * **  *
    0x0BD0, // Row 5:* *  * * (transition to ball)
    0x1FF8, // Row 6:********** (ball starts)
    0x3FFC, // Row 7:************
    0x3FFC, // Row 8:************
    0x3FFC, // Row 9:************
    0x1FF8, // Row 10:**********
    0x0FF0, // Row 11:********
    0x0000,
    0x0000,
    0x0000,
    0x0000
};
static CHIP_RAM_DATA UWORD g_HomingShotDn_Body[16] = {
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x1FF8, // Row 6:**********
    0x3FFC, // Row 7:************
    0x3FFC, // Row 8:************
    0x3FFC, // Row 9:************
    0x1FF8, // Row 10:**********
    0x0FF0, // Row 11:********
    0x0000,
    0x0000,
    0x0000,
    0x0000
};
static CHIP_RAM_DATA UWORD g_HomingShotDn_Accent[16] = {
    0x0180, // Row 0:    **
    0x0180, // Row 1:    **
    0x03C0, // Row 2:   ****
    0x0240, // Row 3:  *    *
    0x05A0, // Row 4: * **  *
    0x0BD0, // Row 5:* *  * *
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000
};

// 2. Moving Down-Left (trail pointing Up-Right)
static CHIP_RAM_DATA UWORD g_HomingShotDL_Mask[16] = {
    0x000C, // Row 0:       **
    0x001C, // Row 1:      ***
    0x0038, // Row 2:     ***
    0x0064, // Row 3:    **  *
    0x00D8, // Row 4:   ** **
    0x07B0, // Row 5: ******* *
    0x1FF0, // Row 6:**********
    0x3FE0, // Row 7:***********
    0x3FE0, // Row 8:***********
    0x3FE0, // Row 9:***********
    0x1FF0, // Row 10:**********
    0x0F60, // Row 11:*********
    0x0000,
    0x0000,
    0x0000,
    0x0000
};
static CHIP_RAM_DATA UWORD g_HomingShotDL_Body[16] = {
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x1FF0, // Row 6:**********
    0x3FE0, // Row 7:***********
    0x3FE0, // Row 8:***********
    0x3FE0, // Row 9:***********
    0x1FF0, // Row 10:**********
    0x0F60, // Row 11:*********
    0x0000,
    0x0000,
    0x0000,
    0x0000
};
static CHIP_RAM_DATA UWORD g_HomingShotDL_Accent[16] = {
    0x000C, // Row 0:       **
    0x001C, // Row 1:      ***
    0x0038, // Row 2:     ***
    0x0064, // Row 3:    **  *
    0x00D8, // Row 4:   ** **
    0x07B0, // Row 5: ******* *
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000
};

// 3. Moving Down-Right (trail pointing Up-Left)
static CHIP_RAM_DATA UWORD g_HomingShotDR_Mask[16] = {
    0x3000, // Row 0: **
    0x3800, // Row 1: ***
    0x1C00, // Row 2:  ***
    0x2600, // Row 3: *  **
    0x1B00, // Row 4:  ** **
    0x0DE0, // Row 5: * *******
    0x0FF8, // Row 6:  **********
    0x07FC, // Row 7:   ***********
    0x07FC, // Row 8:   ***********
    0x07FC, // Row 9:   ***********
    0x0FF8, // Row 10:  **********
    0x06F0, // Row 11:   *********
    0x0000,
    0x0000,
    0x0000,
    0x0000
};
static CHIP_RAM_DATA UWORD g_HomingShotDR_Body[16] = {
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0FF8, // Row 6:  **********
    0x07FC, // Row 7:   ***********
    0x07FC, // Row 8:   ***********
    0x07FC, // Row 9:   ***********
    0x0FF8, // Row 10:  **********
    0x06F0, // Row 11:   *********
    0x0000,
    0x0000,
    0x0000,
    0x0000
};
static CHIP_RAM_DATA UWORD g_HomingShotDR_Accent[16] = {
    0x3000, // Row 0: **
    0x3800, // Row 1: ***
    0x1C00, // Row 2:  ***
    0x2600, // Row 3: *  **
    0x1B00, // Row 4:  ** **
    0x0DE0, // Row 5: * *******
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000
};




