#pragma once
// ============================================================================
// NAU 32x24 - Estil Xenon 2 / Marc Coleman
// Format: 2 words (32 bits) x 24 rows
// Colors: 5 bitplanes per a 32 colors
// ============================================================================

// MÀSCARA - Defineix la forma de la nau (1=opaqua, 0=transparent)
// 32 pixels d'ample = 2 UWORD per fila
// 24 files d'alçada

static const UWORD SHIP32_MASK[24*2] = {
    // Fila 0-1: Proa fina i punxeguda (nose cone)
    0x0000, 0x0180,  //      ***
    0x0000, 0x03C0,  //     *****
    
    // Fila 2-3: Proa s'amplia  
    0x0000, 0x07E0,  //    *******
    0x0080, 0x0FF0,  //   *********
    
    // Fila 4-5: Secció de cockpit, comença ales
    0x01C0, 0x1FF8,  //  ***********
    0x03E0, 0x3FFC,  // *************
    
    // Fila 6-7: Ales completes, cos màxim
    0x07F0, 0x7FFE,  // ***************
    0x0FF8, 0xFFFF,  // ****************
    
    // Fila 8-9: Centre de la nau (secció més ampla)
    0x1FFC, 0xFFFF,  // ****************
    0x3FFE, 0xFFFF,  // ****************
    
    // Fila 10-11: Cos central, comença estretament per motors
    0x7FFF, 0xFFFE,  // ***************
    0x7FFF, 0xFFFE,  // ***************
    
    // Fila 12-13: Tapering cap als motors dobles
    0x3FFE, 0xFFFF,  // ****************
    0x1FFC, 0xE007,  //  ***       ***
    
    // Fila 14-15: Motors esquerre i dret (dues protuberàncies)
    0x0FF8, 0xC003,  // **         **
    0x07F0, 0x8001,  // *           *
    
    // Fila 16-17: Baixada dels motors
    0x03E0, 0x8001,  // *           *
    0x01C0, 0xC003,  // **         **
    
    // Fila 18-19: Puntes dels motors amb llum
    0x0080, 0xE007,  //  ***     ***
    0x0000, 0x700E,  //   ***   ***
    
    // Fila 20-21: Brillantor final dels propulsors
    0x0000, 0x381C,  //    ******* 
    0x0000, 0x1C38,  //     *****
    
    // Fila 22-23: Puntes de flama dels motors
    0x0000, 0x0C30,  //      ***
    0x0000, 0x0600,  //       *
};

// ============================================================================
// BITPLANE 0: Cos metàl·lic base (gris fosc, ombrejat)
// Color: Índexs 8-11 (grisos metàl·lics)
// ============================================================================

static const UWORD SHIP32_PLANE0[24*2] = {
    // Proa
    0x0000, 0x0180,
    0x0000, 0x03C0,
    0x0000, 0x07E0,
    0x0080, 0x0FF0,
    // Cockpit + ales
    0x01C0, 0x1FF8,
    0x03E0, 0x3FFC,
    0x07F0, 0x7FFE,
    0x0FF8, 0xFFFF,
    // Cos central
    0x1FFC, 0xFFFF,
    0x3FFE, 0xFFFF,
    0x7FFF, 0xFFFE,
    0x7FFF, 0xFFFE,
    // Tapering
    0x3FFE, 0xFFFF,
    0x1FFC, 0xE007,
    // Motors
    0x0FF8, 0xC003,
    0x07F0, 0x8001,
    0x03E0, 0x8001,
    0x01C0, 0xC003,
    0x0080, 0xE007,
    0x0000, 0x700E,
    0x0000, 0x381C,
    0x0000, 0x1C38,
    0x0000, 0x0C30,
    0x0000, 0x0600,
};

// ============================================================================
// BITPLANE 1: Metàl·lic mig (vores, detalls)
// Color: Índexs 16-19 (gris mig, brillantor metàl·lica)
// ============================================================================

static const UWORD SHIP32_PLANE1[24*2] = {
    // Vores de la proa
    0x0000, 0x0000,
    0x0000, 0x0180,
    0x0000, 0x03C0,
    0x0000, 0x07E0,
    // Vores del cos
    0x0080, 0x0FF0,
    0x01C0, 0x1FF8,
    0x03E0, 0x3FFC,
    0x07F0, 0x7FFE,
    // Centre amb detall
    0x0FF8, 0x7FFE,
    0x1FFC, 0x3FFC,
    0x3FFE, 0x7FFE,
    0x3FFE, 0x7FFE,
    // Tapering
    0x1FFC, 0x3FFC,
    0x0FF8, 0x6006,
    // Motors - anells
    0x07F0, 0x4002,
    0x03E0, 0x0000,
    0x01C0, 0x0000,
    0x0080, 0x4002,
    0x0000, 0x6006,
    0x0000, 0x381C,
    0x0000, 0x1C38,
    0x0000, 0x0E10,
    0x0000, 0x0600,
    0x0000, 0x0200,
};

// ============================================================================
// BITPLANE 2: Metàl·lic brillant (reflexos, zones llises)
// Color: Índexs 24-27 (blanc metàl·lic, reflexos)
// ============================================================================

static const UWORD SHIP32_PLANE2[24*2] = {
    // Punta brillant
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0180,
    0x0000, 0x03C0,
    // Reflexos del cos
    0x0000, 0x07E0,
    0x0080, 0x0FF0,
    0x01C0, 0x1FF8,
    0x03E0, 0x3FFC,
    // Zona central brillant
    0x07F0, 0x3FFC,
    0x0FF8, 0x1FF8,
    0x1FFC, 0x3FFC,
    0x1FFC, 0x3FFC,
    // Reflexos als motors
    0x0FF8, 0x1FF8,
    0x07F0, 0x380C,
    0x03E0, 0x3006,
    0x01C0, 0x2002,
    0x0080, 0x2002,
    0x0000, 0x3006,
    0x0000, 0x380C,
    0x0000, 0x1C38,
    0x0000, 0x0E10,
    0x0000, 0x0600,
    0x0000, 0x0200,
};

// ============================================================================
// BITPLANE 3: Cockpit i detalls (Verd cian / blau elèctric)
// Color: Índexs 4-7 (verds/cian brillants)
// ============================================================================

static const UWORD SHIP32_PLANE3[24*2] = {
    // No color a la proa
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    // COCKPIT - forma de gota al centre
    0x0040, 0x0800,  //   *     *
    0x00E0, 0x1C00,  //  ***   ***
    0x01F0, 0x3E00,  // ***** *****
    0x03F8, 0x7F00,  //*************
    // Centre de la nau amb detall
    0x07FC, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    // No color als motors
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
};

// ============================================================================
// BITPLANE 4: Motors de foc (Vermell/Taronja brillant)
// Color: Índexs 28-31 (vermell foc, taronja)
// ============================================================================

static const UWORD SHIP32_PLANE4[24*2] = {
    // No foc a la proa
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    0x0000, 0x0000,
    // FOC ALS MOTORS - llums dels propulsors
    0x0000, 0x600C,  // **       **
    0x0000, 0x3018,  //  **     **
    0x0000, 0x1830,  //   **   **
    0x0000, 0x0C30,  //    ** **
    0x0000, 0x0600,  //      *
};

// ============================================================================
// METADADES
// ============================================================================

#define SHIP32_WIDTH_WORDS  2   // 32 pixels = 2 words de 16 bits
#define SHIP32_HEIGHT       24  // 24 files
#define SHIP32_NUM_PLANES   5   // 5 bitplanes = 32 colors
