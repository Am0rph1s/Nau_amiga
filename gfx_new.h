#pragma once
// ============================================================================
// NAU DX AMIGA - Sprite/Bob graphics data (Format "Xenon 2 Enhanced")
// Mides flexibles: qualsevol amplada (múltiple de 16) i alçada
// Format per bob: mask + data per bitplane, non-interleaved
//
// Estructura de dades:
//   - Mask: 1 word per cada 16 pixels d'amplada, per fila
//   - Data: N words per fila (on N = width/16), per cada bitplane usat
//
// Colors via palette (32 colors total, 5 bitplanes):
//   Ship body        -> color index 4-7 (metàl·lics)
//   Ship cockpit     -> color index 8-11 (verds/cian brillants)
//   Ship motors      -> color index 12-15 (taronja/vermell foc)
//   Enemy small      -> color index 16-19 (biològic: verds/porpra)
//   Enemy medium     -> color index 20-23 (metàl·lic: grisos/blaus)
//   Enemy large      -> color index 24-27 (armadura: daurats/cuivre)
//   Boss             -> color index 28-31 (complex multi-color)
//   Shot player      -> color index 30 (groc brillant)
//   Shot enemy       -> color index 29 (vermell)
//   Explosion        -> color index 28-31 (taronja/vermell/groc)
// ============================================================================

// --- SHIP "MEGABLAST-STYLE" (32x24 pixels = 2 words x 24 rows) ---
// Disseny: nau compacta, metàl·lica, amb motors brillants i escut cockpit
// Format: 2 UWORD per fila de mask, 2 UWORD x 5 planes per fila de data

// Ship 24x20 (2 words wide x 20 rows) - Cleaner Xenon 2 style design
static const UWORD g_ShipNew_Mask[20*2] = {
    // row 0-1: Sharp nose cone
    0x0000, 0x0180, 0x0000, 0x03C0,
    // row 2-3: Nose widens
    0x0000, 0x07E0, 0x0080, 0x0FF0,
    // row 4-5: Main fuselage with wings starting
    0x01C0, 0x1FF8, 0x03E0, 0x3FFC,
    // row 6-7: Full wingspan
    0x07F0, 0x7FFE, 0x0FF8, 0xFFFF,
    // row 8-9: Center section with weapon pods
    0x1FFC, 0xFFFF, 0x3FFE, 0xFFFF,
    // row 10-11: Lower fuselage
    0x7FFF, 0xFFFE, 0x7FFF, 0xFFFE,
    // row 12-13: Tapering to engines
    0x3FFE, 0xFFFF, 0x1FFC, 0xE007,
    // row 14-15: Twin engine housings
    0x0FF8, 0xC003, 0x07F0, 0x8001,
    // row 16-17: Engine exhaust ports
    0x03E0, 0x8001, 0x01C0, 0xC003,
    // row 18-19: Engine glow/thrusters
    0x0080, 0xE007, 0x0000, 0x700E,
};

// Data per cada bitplane (5 planes = 32 colors)
// Plane 0: bit 0 de color (fons/metàl·lic fosc)
// Plane 1: bit 1 de color (metàl·lic mig)
// Plane 2: bit 2 de color (metàl·lic brillant)
// Plane 3: bit 3 de color (detalls/cockpit)
// Plane 4: bit 4 de color (motors/lums)

// Bitplane 0: Metàl·lic fosc (base del cos)
static const UWORD g_ShipNew_Plane0[24*2] = {
    0x0000, 0x0180, 0x0000, 0x03C0, 0x0000, 0x07E0,
    0x0080, 0x0FF0, 0x01C0, 0x1FF8, 0x03E0, 0x3FFC,
    0x07F0, 0x7FFE, 0x0FF8, 0xFFFF, 0x1FFC, 0xFFFF,
    0x3FFE, 0xFFFF, 0x7FFF, 0xFFFE, 0x7FFF, 0xFFFE,
    0x3FFE, 0xFFFF, 0x1FFC, 0xE007, 0x0FF8, 0xC003,
    0x07F0, 0x8001, 0x03E0, 0x8001, 0x01C0, 0xC003,
    0x0080, 0xE007, 0x0000, 0x700E, 0x0000, 0x381C,
    0x0000, 0x1C38, 0x0000, 0x0C30, 0x0000, 0x0600,
};

// Bitplane 1: Metàl·lic mig (ombrejat, vores)
static const UWORD g_ShipNew_Plane1[24*2] = {
    0x0000, 0x0000, 0x0000, 0x0180, 0x0000, 0x03C0,
    0x0000, 0x07E0, 0x0080, 0x0FF0, 0x01C0, 0x1FF8,
    0x03E0, 0x3FFC, 0x07F0, 0x7FFE, 0x0FF8, 0x7FFE,
    0x1FFC, 0x3FFC, 0x3FFE, 0x7FFE, 0x3FFE, 0x7FFE,
    0x1FFC, 0x3FFC, 0x0FF8, 0x700E, 0x07F0, 0x6006,
    0x03E0, 0x4002, 0x01C0, 0x4002, 0x0080, 0x6006,
    0x0000, 0x700E, 0x0000, 0x381C, 0x0000, 0x1C38,
    0x0000, 0x0C30, 0x0000, 0x0400, 0x0000, 0x0200,
};

// Bitplane 2: Metàl·lic brillant (reflexos, vores clares)
static const UWORD g_ShipNew_Plane2[24*2] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0180,
    0x0000, 0x03C0, 0x0000, 0x07E0, 0x0080, 0x0FF0,
    0x01C0, 0x1FF8, 0x03E0, 0x3FFC, 0x07F0, 0x3FFC,
    0x0FF8, 0x1FF8, 0x1FFC, 0x3FFC, 0x1FFC, 0x3FFC,
    0x0FF8, 0x1FF8, 0x07F0, 0x380C, 0x03E0, 0x3006,
    0x01C0, 0x2002, 0x0080, 0x2002, 0x0000, 0x3006,
    0x0000, 0x380C, 0x0000, 0x1C38, 0x0000, 0x0E10,
    0x0000, 0x0600, 0x0000, 0x0200, 0x0000, 0x0000,
};

// Bitplane 3: Cockpit verd/cian brillant
static const UWORD g_ShipNew_Plane3[24*2] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0040, 0x0800, 0x00E0, 0x1C00,
    0x01F0, 0x3E00, 0x03F8, 0x7F00, 0x07FC, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

// Bitplane 4: Motors foc vermell/taronja brillant
static const UWORD g_ShipNew_Plane4[24*2] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x600C, 0x0000, 0x3018,
    0x0000, 0x1830, 0x0000, 0x0820, 0x0000, 0x0400,
};

// ... Plane 2, 3, 4 quan els necessitis

// --- ENEMICS MULTI-MIDA (Xenon 2 style) ---

// Enemy Small 16x16 (biològic, ràpid) - Forma insecte/àcar amb ulls
static const UWORD g_EnemySmall_Mask[16] = {
    0x03C0, 0x07E0, 0x0FF0, 0x1FF8,
    0x3FFC, 0x7FFE, 0x7FFE, 0xFFFF,
    0xFFFF, 0x7FFE, 0x7FFE, 0x3FFC,
    0x1FF8, 0x0FF0, 0x07E0, 0x03C0,
};
static const UWORD g_EnemySmall_Plane0[16] = {
    0x03C0, 0x07E0, 0x0FF0, 0x1FF8,
    0x3FFC, 0x7FFE, 0x7FFE, 0xFFFF,
    0xFFFF, 0x7FFE, 0x7FFE, 0x3FFC,
    0x1FF8, 0x0FF0, 0x07E0, 0x03C0,
};
static const UWORD g_EnemySmall_Plane3[16] = {  // Ulls brillants
    0x0000, 0x0000, 0x0810, 0x1818,
    0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000,
    0x1818, 0x0810, 0x0000, 0x0000,
};

// Enemy Medium 24x24 (metàl·lic, mitjà)
static const UWORD g_EnemyMedium_Mask[24*2] = { 0 };  // 24px = 2 words
static const UWORD g_EnemyMedium_Planes[24*2*5] = { 0 };

// Enemy Large 32x32 (armadura, lent però resistent)
static const UWORD g_EnemyLarge_Mask[32*2] = { 0 };
static const UWORD g_EnemyLarge_Planes[32*2*5] = { 0 };

// Boss 48x40 (complex, multi-secció)
static const UWORD g_BossNew_Mask[40*3] = { 0 };  // 48px = 3 words
static const UWORD g_BossNew_Planes[40*3*5] = { 0 };

// --- Estructures de metadades per dibuix ---

typedef struct {
    const UWORD* mask;
    const UWORD* planes[5];  // Fins a 5 bitplanes
    UBYTE width_words;       // Amplada en paraules (width/16)
    UBYTE height;            // Alçada en files
    UBYTE num_planes;        // Bitplanes usats (1-5)
} TSpriteDef;

// Definicions per accés ràpid
#define SHIP_NEW_WIDTH_WORDS    2   // 32 pixels
#define SHIP_NEW_HEIGHT         24

#define ENEMY_SMALL_WW          1   // 16px
#define ENEMY_SMALL_H           16

#define ENEMY_MEDIUM_WW         2   // 24px (arrodonit a 32)
#define ENEMY_MEDIUM_H          24

#define ENEMY_LARGE_WW          2   // 32px
#define ENEMY_LARGE_H           32

#define BOSS_NEW_WW             3   // 48px
#define BOSS_NEW_H              40

// ============================================================================
// INSTRUCCIONS PER CREAR ELS TEUS SPRITES:
//
// 1. Obre un editor gràfic (GIMP, Photoshop, Aseprite, etc.)
// 2. Crea una imatge de la mida desitjada (ex: 32x24 per la nau)
// 3. Dibuixa amb colors de la paleta Amiga (màxim 32 colors)
// 4. Guarda com PNG o BMP
// 5. Jo et passaré un script Python per convertir-ho a aquest format
//
// O si prefereixes editar directament:
// - Cada UWORD = 16 píxels horitzontals
// - Bit 15 = píxel més a l'esquerra, Bit 0 = més a la dreta
// - 0xFFFF = 16 píxels plens, 0x0000 = 16 píxels buits
// - 0xFF00 = 8 píxels a l'esquerra plens, 8 a la dreta buits
// ============================================================================
