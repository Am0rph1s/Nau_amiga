#pragma once
// Auto-generated from sprites/Nau.png - full colour 5-bitplane version
// Amiga palette: 12 colours + transparent bg
#define SHIP_ANIM_WIDTH_WORDS 2
#define SHIP_ANIM_HEIGHT      24
#define SHIP_ANIM_NUM_FRAMES  4

// Call SetShipPalette() once after copper list init to load ship colours
// Adjust slot offset to match your game's palette layout (default: slots 1-12)
#define SHIP_PAL_OFFSET 1
static const UWORD SHIP_PALETTE_12BIT[] = {
    0x112,  // slot 1
    0x224,  // slot 2
    0x346,  // slot 3
    0x68A,  // slot 4
    0x468,  // slot 5
    0x311,  // slot 6
    0x631,  // slot 7
    0xB51,  // slot 8
    0xFA3,  // slot 9
    0x031,  // slot 10
    0x9AC,  // slot 11
    0xDEF,  // slot 12
};

static const UWORD SHIP_MASK[24*2] = {
    0x0000, 0x0000,  // row 0
    0x0003, 0xC000,  // row 1
    0x0003, 0xC000,  // row 2
    0x0007, 0xE000,  // row 3
    0x0007, 0xE000,  // row 4
    0x0007, 0xE000,  // row 5
    0x001F, 0xF800,  // row 6
    0x003F, 0xFC00,  // row 7
    0x003F, 0xFC00,  // row 8
    0x003F, 0xFC00,  // row 9
    0x003F, 0xFC00,  // row 10
    0x003F, 0xFC00,  // row 11
    0x003F, 0xFC00,  // row 12
    0x007F, 0xFE00,  // row 13
    0x00FF, 0xFF00,  // row 14
    0x31FF, 0xFF8C,  // row 15
    0x33FF, 0xFFCC,  // row 16
    0x37FF, 0xFFEC,  // row 17
    0x3FFF, 0xFFFC,  // row 18
    0x3FFF, 0xFFFC,  // row 19
    0x3FFF, 0xFFFC,  // row 20
    0x3FFF, 0xFFFC,  // row 21
    0x00F0, 0x0F00,  // row 22
    0x0000, 0x0000,  // row 23
};

static const UWORD SHIP_PLANE0[24*2] = {
    0x0000, 0x0000,  // row 0
    0x0001, 0xC000,  // row 1
    0x0003, 0xC000,  // row 2
    0x0007, 0x6000,  // row 3
    0x0007, 0xC000,  // row 4
    0x0001, 0xC000,  // row 5
    0x0000, 0xA800,  // row 6
    0x0031, 0xA000,  // row 7
    0x0013, 0xB800,  // row 8
    0x0039, 0x7800,  // row 9
    0x003E, 0x3000,  // row 10
    0x0019, 0x1800,  // row 11
    0x003C, 0x9400,  // row 12
    0x0036, 0xA000,  // row 13
    0x00FF, 0x7100,  // row 14
    0x10FF, 0xB908,  // row 15
    0x119B, 0x3D4C,  // row 16
    0x31CA, 0x37A4,  // row 17
    0x17EE, 0xB7B4,  // row 18
    0x136E, 0xD694,  // row 19
    0x17D5, 0xC5B4,  // row 20
    0x364E, 0xF2C4,  // row 21
    0x0040, 0x0800,  // row 22
    0x0000, 0x0000,  // row 23
};

static const UWORD SHIP_PLANE1[24*2] = {
    0x0000, 0x0000,  // row 0
    0x0002, 0xC000,  // row 1
    0x0002, 0xC000,  // row 2
    0x0001, 0xC000,  // row 3
    0x0005, 0xA000,  // row 4
    0x0006, 0xA000,  // row 5
    0x001E, 0x5000,  // row 6
    0x003E, 0x5400,  // row 7
    0x002F, 0xCC00,  // row 8
    0x0003, 0xA400,  // row 9
    0x0027, 0xEC00,  // row 10
    0x0024, 0x6C00,  // row 11
    0x0012, 0x6800,  // row 12
    0x002C, 0x7C00,  // row 13
    0x0077, 0xEA00,  // row 14
    0x20F6, 0x4904,  // row 15
    0x2197, 0xCDC0,  // row 16
    0x32F6, 0x4F6C,  // row 17
    0x36F2, 0x4F6C,  // row 18
    0x3CD0, 0x0B7C,  // row 19
    0x3EAB, 0x3E7C,  // row 20
    0x3BD7, 0x4B3C,  // row 21
    0x00B0, 0x0700,  // row 22
    0x0000, 0x0000,  // row 23
};

static const UWORD SHIP_PLANE2[24*2] = {
    0x0000, 0x0000,  // row 0
    0x0001, 0x0000,  // row 1
    0x0001, 0x0000,  // row 2
    0x0002, 0x0000,  // row 3
    0x0004, 0x2000,  // row 4
    0x0005, 0x2000,  // row 5
    0x0005, 0x8000,  // row 6
    0x0005, 0x8800,  // row 7
    0x0014, 0x0000,  // row 8
    0x0000, 0x2000,  // row 9
    0x0016, 0x6000,  // row 10
    0x0010, 0x6000,  // row 11
    0x0021, 0x8400,  // row 12
    0x004B, 0x9200,  // row 13
    0x00B0, 0x0900,  // row 14
    0x2171, 0x8880,  // row 15
    0x0290, 0x0D00,  // row 16
    0x04F1, 0x8F00,  // row 17
    0x08F5, 0xAF10,  // row 18
    0x0025, 0xA540,  // row 19
    0x0C20, 0x8070,  // row 20
    0x0020, 0x8440,  // row 21
    0x0080, 0x0000,  // row 22
    0x0000, 0x0000,  // row 23
};

static const UWORD SHIP_PLANE3[24*2] = {
    0x0000, 0x0000,  // row 0
    0x0000, 0x0000,  // row 1
    0x0000, 0x0000,  // row 2
    0x0000, 0x8000,  // row 3
    0x0000, 0x0000,  // row 4
    0x0000, 0x0000,  // row 5
    0x0003, 0x0000,  // row 6
    0x0012, 0x0000,  // row 7
    0x0020, 0x0000,  // row 8
    0x0006, 0x8000,  // row 9
    0x0001, 0x8C00,  // row 10
    0x0003, 0x8000,  // row 11
    0x0002, 0x0800,  // row 12
    0x0000, 0x4400,  // row 13
    0x0000, 0x8400,  // row 14
    0x0000, 0x4600,  // row 15
    0x0060, 0x8200,  // row 16
    0x0200, 0x4000,  // row 17
    0x2000, 0x0040,  // row 18
    0x2C00, 0x0020,  // row 19
    0x200A, 0x0200,  // row 20
    0x0010, 0x0900,  // row 21
    0x0000, 0x0000,  // row 22
    0x0000, 0x0000,  // row 23
};

static const UWORD SHIP_PLANE4[24*2] = {
    0x0000, 0x0000,  // row 0
    0x0000, 0x0000,  // row 1
    0x0000, 0x0000,  // row 2
    0x0000, 0x0000,  // row 3
    0x0000, 0x0000,  // row 4
    0x0000, 0x0000,  // row 5
    0x0000, 0x0000,  // row 6
    0x0000, 0x0000,  // row 7
    0x0000, 0x0000,  // row 8
    0x0000, 0x0000,  // row 9
    0x0000, 0x0000,  // row 10
    0x0000, 0x0000,  // row 11
    0x0000, 0x0000,  // row 12
    0x0000, 0x0000,  // row 13
    0x0000, 0x0000,  // row 14
    0x0000, 0x0000,  // row 15
    0x0000, 0x0000,  // row 16
    0x0000, 0x0000,  // row 17
    0x0000, 0x0000,  // row 18
    0x0000, 0x0000,  // row 19
    0x0000, 0x0000,  // row 20
    0x0000, 0x0000,  // row 21
    0x0000, 0x0000,  // row 22
    0x0000, 0x0000,  // row 23
};

#define SHIP_FRAME0_MASK SHIP_MASK
#define SHIP_FRAME0_DATA SHIP_MASK
#define SHIP_FRAME1_MASK SHIP_MASK
#define SHIP_FRAME1_DATA SHIP_MASK
#define SHIP_FRAME2_MASK SHIP_MASK
#define SHIP_FRAME2_DATA SHIP_MASK
#define SHIP_FRAME3_MASK SHIP_MASK
#define SHIP_FRAME3_DATA SHIP_MASK

static const UWORD* const SHIP_ANIM_MASKS[4] = {
    SHIP_FRAME0_MASK, SHIP_FRAME1_MASK, SHIP_FRAME2_MASK, SHIP_FRAME3_MASK
};
static const UWORD* const SHIP_ANIM_DATA[4] = {
    SHIP_FRAME0_DATA, SHIP_FRAME1_DATA, SHIP_FRAME2_DATA, SHIP_FRAME3_DATA
};

static const UWORD* const SHIP_PLANES[5] = {
    SHIP_PLANE0, SHIP_PLANE1, SHIP_PLANE2, SHIP_PLANE3, SHIP_PLANE4
};
