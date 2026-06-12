# Progress: Dual-Playfield Implementation

## Status
Dual-playfield mode is implemented and compiling. PF2 (game sprites) renders on top of PF1 (background/walls).

## Force Field (current)
- 1-bit **banded-dither** circular bubble drawn into BPL6 of PF2 (Sonic 1
  water-bubble look). The disc (r=22, 48x48) is filled with a 4x4 Bayer
  threshold pattern at ~50% density, so the background shows through the
  unset pixels -> a translucent "dome" feel without true alpha. The ship's
  draw still clears BPL6 in its bounding box, so the visible result is the
  banded-dither annulus around the ship. (g_FFBubScroll0..3)
- Bubble size cycles through 4 scroll frames at 6.25 fps so the 3-dense /
  3-sparse row bands appear to flow vertically.
- **Polarity flip transition**: 4-frame window. Frames 1-4: a "color sweep"
  is drawn AFTER the ship so the dome covers it. g_FFSweepSrc0..3 and
  g_FFSweepDst0..3 are solid-disc masks split by a vertical wiper at
  x=2,16,30,44. BPL4 gets the source mask (old polarity color) and BPL6
  gets the destination mask (new polarity). Same masks reused for both
  directions by swapping BPL4/BPL6 and reversing the frame order:
  - white->black: wiper goes L->R, BPL4=blue, BPL6=red
  - black->white: wiper goes R->L, BPL4=red, BPL6=blue
- Polarity colors (Ikaruga-style):
  - White polarity: 0x0CF (light blue) ↔ 0x0FF (cyan)
  - Black polarity: 0xA00 (dark red) ↔ 0xF00 (red)
  - Transition: 0x0CF/0xF00 split between BPL4 and BPL6
- Color: written to `g_Palette[10]` and `g_Palette[12]` (the Copper
  reloads them from there on every frame via `BuildCopperListEx`; writing
  `custom->color[]` directly is silently overwritten by the Copper).
- Hardware-sprite approach (SPR4 / DMA 4) was abandoned: in OCS dual-playfield
  mode hardware sprites are rendered behind both playfields.

## Roadmap: Dual Shot + Polarity Absorption + ASM Optimization

### Ideas pendientes (recordatori)
- [ ] **Dual shot** (Ikaruga-style): la nau dispara 2 trets, un per cada ala,
      rectes i no inclinats. Reutilitzen la mateixa màscara (`g_ShotMask`).
      Cal ampliar `MAX_SHOTS` per allotjar els trets extra.
- [ ] **Cadència de foc més ràpida**: reduir `FIRE_COOLDOWN` de 3 a 1-2.
- [ ] **Absorció de trets per polaritat** (Ikaruga-style): quan un tret
      enemic té la MATEIXA polaritat que la nau i està a prop, la nau
      l'absorbeix. El tret absorbit esdevé un tret amic afegit al pool del
      jugador. Això provoca que la pantalla s'ompli ràpidament de trets
      quan la polaritat és correcta.
- [ ] **`MAX_SHOTS` ampliat** (4 → 16-24) per allotjar els trets absorbits.
- [ ] **Feedback visual d'absorció**: partícules/glow quan s'absorbeix un
      tret. Color segons la polaritat.

### Optimitzacions ASM planificades
El motor actual té ~5 bucles d'entitats en C que consumeixen ~5-15% del
frame (depenent de quantes entitats hi ha actives). El motiu principal
per passar-les a ASM és que les absorcions massives (15-20+ trets a la
pantalla) saturaran la CPU. Les rutines crítiques són:

| # | Rutina | Línies actuals | Benefici ASM |
|---|--------|----------------|--------------|
| 1 | `UpdatePlayerShots` | main.c:1994-1998 | 3x |
| 2 | `UpdateEnemyShots` + ship-collision | main.c:2051-2071 | 3-4x |
| 3 | `UpdateEnemies` + shot-collision + ship-collision | main.c:2001-2048 | 4x (la més calenta) |
| 4 | `EnemyFire` | main.c:2074-2088 | 3x |
| 5 | `UpdateExplosions` | main.c:2091-2096 | 2x (trivial) |

Pla: implementar-les a `blitter.s` (que ja conté les rutines de dibuix)
o a un nou `entities.s`. Convencions m68k: A6 = globals, D0/D1/A0/A1 = scratch.

### Tècniques ASM a aplicar
- **DBRA** per bucles: `dbra d0, .loop` (4 cicles, decrement + branch)
- **Adreces pre-computades**: carregar `lea g_Shots, a0` un cop, usar offset
  per accedir a cada element
- **Estructura de dades compacta**: TShot amb 2 shorts (x, y) + 1 byte
  (active) + 1 byte (variant) = 6 bytes, accés directe amb `addq.w #6, a0`
- **Branches optimitzats**: `bge.s` (no `bge.w`) per shortcuts dins del
  bucle
- **Evitar acces a memòria redundant**: cachejar valors en registres
- **Màscares AABB**: `(x0+w0 > x1) && (x0 < x1+w1) && ...` es pot fer
  amb 4 CMP + 4 Bxx

### Decisió estratègica
Fem 1-4 (dual shot + cadència + absorció + MAX_SHOTS) EN C PRIMER
per validar la mecànica de joc. DESPRÉS movem les 5 rutines a ASM.
La raó: validar gameplay és més ràpid en C, i l'ASM el podem afinar
un cop sabem quantes entitats típiques tenim.

## Fet recentment: 5 rutines d'update d'entitats en ASM
- `entities.s` (nou) conté `AsmUpdatePlayerShots`, `AsmUpdateEnemyShots`,
  `AsmUpdateEnemies`, `AsmUpdateExplosions`, `AsmEnemyFire` en ASM pur.
- `blitter.h` declara els externs.
- `main.c` substitueix els 5 bucles C per crides a aquestes rutines; les
  col·lisions (shot-enemy, enemy-ship, shot-ship) es queden en C.
- Globals (`g_Shots`, `g_Enemies`, etc.) trets de `static` perquè
  l'assemblador els pugui resoldre; s'ha hagut de treure `-fwhole-program`
  del Makefile (amagava tots els símbols).
- Mides: `AsmUpdatePlayerShots` ≈ 40 bytes, la resta entre 40-80 bytes
  cadascuna. Total ~300 bytes en text, molt modest.
- Avantatges: cada bucle d'update té només els `tst.w + adda.w + dbra`
  essencials, sense comprovacions d'índexos ni accessos `&g_X[i]` que el
  compilador C ha d'expandir a adreces/registres cada iteració.

## What is Done

### Hardware Setup
- [x] BPLCON0: dual-playfield bit 10 enabled + 6 bitplanes (BPL1..6)
- [x] BPLCON2: PF2PRI bit 6 set (game in front of background)
- [x] Copper list: interleaved plane pointers (BPL1=PF1_0, BPL2=PF2_0, BPL3=PF1_1, BPL4=PF2_1, BPL5=PF1_2, BPL6=PF2_2)
- [x] Force field ring on BPL6 (PF2 bit 2), color via palette[12]

### Palette
- [x] Slots 0-7: PF1 colors (background + walls)
- [x] Slots 6-7: wall rock colors (0x0321 dark, 0x0654 light)
- [x] Slot 8: PF2 transparent (shows PF1 behind)
- [x] Slots 9-11: PF2 greyscale (dark 0x0444, mid 0x0888, light 0x0CCC)
- [x] Slots 12-15: PF2 accent colors (yellow, orange, red, white)

### Clear Routine
- [x] ClearGameAreaAsm: clears only planes 1 and 3 (PF2), full 40 bytes/row

### Drawing Functions (all PF2-only now)
- [x] DrawBob16: writes only to planes 1,3; colorMask bit0->plane1, bit1->plane3
- [x] DrawBob32: writes only to planes 1,3
- [x] DrawBob16_2bpl: direct write to specified planes (1,3)
- [x] DrawBob32_2bpl: direct write to specified planes (1,3)
- [x] DrawShipAnim: merges original 4 planes into 2 (planes 0|1->plane1, planes 2|3->plane3)
- [x] DrawPixel: writes only to planes 1,3; colorIdx bit0->plane1, bit1->plane3

### Parallax Walls (now in PF1)
- [x] ParallaxInitWalls: sets planes 2,4 to 0xFF (PF1 wall bits 1,2)
- [x] ParallaxDraw: writes only plane 0 (PF1 tile pattern), planes 2,4 untouched

### Enemy / Shot / Explosion Colors
- [x] g_EnemyColor updated for PF2 values (1,2,3)
- [x] Boss draws: white=0x03, red=0x01
- [x] Shots: 0x03 (light grey/white)
- [x] Explosions: 0x03

### Starfield
- [x] g_StarsEnabled flag added (default 0 for planet)
- [x] Star drawing gated behind flag
- Code preserved for space levels

### Safety
- [x] Bottom clipping added to all draw functions (y + rows > SCREEN_H)

## Known Issues / TODO

### Critical
- [ ] **Colors are wrong**: everything appears black/white/grey. Likely causes:
  - BPLCON0/BPLCON2 values may need verification against real hardware docs
  - Copper COLOR registers may not be loading correctly in dual-playfield mode
  - PF2 color 0 (slot 8) transparency may be misconfigured

- [ ] **Mask accumulation at top**: sprites leave artifacts. Likely causes:
  - Draw functions may still be touching PF1 planes somewhere
  - Clear may not be fully zeroing PF2 in all edge cases
  - Double buffer swap may not be clean

### Missing Features
- [ ] **Planet background image**: need to add a graphical background to PF1 planes 0,2,4
  - Could be a static image, a scrolling tilemap, or a gradient
  - Must fit within 8 colors (PF1 palette slots 0-7)

- [ ] **Level switching**: mechanism to toggle between space (stars) and planet (background)
  - g_StarsEnabled flag exists but needs wiring to level selection

- [ ] **Enemy sprite assets**: some enemy types still use old 1-bitplane data (heavy, diver, bomber)
  - Should be converted to 2-bitplane greyscale like basic/fast enemies

- [ ] **Ship sprite**: currently merging 4-bitplane greyscale down to 2 planes
  - Result is acceptable but loses detail; could create a proper 2-bitplane ship asset

## Next Steps (in priority order)
1. Fix the color/mask bugs — verify BPLCON0/BPLCON2 values and test on emulator
2. Add a planet background image to PF1
3. Implement level switching between space and planet modes
4. Convert remaining enemy sprites to 2-bitplane assets

## Files Modified
- main.c (copper list, palette, draw functions, RenderFrame, game state)
- blitter.s (ClearGameAreaAsm)
- gfx.h (g_EnemyColor)
- nau_dx.h (ENEMY_W, ENEMY_H)
- gfx_enemy_basic24.h (new)
- gfx_enemy_fast16.h (new)
