# Project: nau_dx_amiga — Ikaruga-style OCS Amiga shooter

## Goal
OCS Amiga (68000) shmup with 1 MB RAM. Implements Ikaruga-style polarity mechanics (white/black), hardware sprite multiplexing for player shots, blitter fallback.

## Build
VS Code extension: **Amiga C/C++ Compile, Debug & Profile** (bartman). Per compilar, obre la paleta de comandos i executa "Amiga: Build" o usa `Ctrl+Shift+B`. Alternativament: `m68k-amiga-elf-gcc -Ofast -flto -fwhole-program`, `gnumake`, `elf2hunk`, `exe2adf`.

## SPR Register Encoding (OCS — Amiga Hardware Reference Manual 3rd ed.)

### SPRxPOS (write-only, $dff140 + 8*n)
- `[15:8]` = VSTART[7:0]
- `[7:0]` = HSTART[8:1] → `(hstart >> 1) & 0xFF`

### SPRxCTL (write-only, $dff142 + 8*n)
- `[11:8]` = VSTOP[3:0] (4 bits a OCS, compare amb scanline[3:0]) → `(vstart + SHOT_H) & 0x0F`
- `[7]` = VSTART[8] (parell) / ATT (senar) → `((vstart>>8)&1)<<7` o `(1<<7)` per attach
- `[0]` = HSTART[0] (LSB) → `hstart & 1`
- `[15]` = **NO USAT a OCS** — no posar ATT aquí!
- `[6]` = VSTOP[4] (ECS/AGA, ignorat a OCS)

### Chip-RAM CTL (mateix format que el registre HW)
- **Parell**: bit 7 = VSTART[8], ATT es llegeix del SENAR
- **Senar**: bit 7 = ATT=1 per attach. VSTART[8] del senar ignora (el parell controla el pair)
- El DMA del canal parell llegeix el CTL del senar via ATT=1 i combina les dades

## Architecture
- **6 bitplanes**: PF1 (bg, 3 planes) + PF2 (game sprites, 3 planes)
- **Copper list**: double-buffered 1024 bytes, rebuilt each frame
- **Sprites**: OCS attached pairs for player shots (max 4 simultaneous per frame, blitter fallback)
- **VBlank handler**: swaps copper list and sprite buffer, sets SPRxPTH/PTL for all 8 channels (chip RAM POS/CTL positions the sprites)

## OCS Sprite System

### Attached Pairs (ATTACH=1)
- **4 pairs**: ch 0+1, 2+3, 4+5, 6+7 (paired as per OCS sprite hardware)
- Even channel = plane 0 (body), Odd channel = plane 1 (accent)
- Chip RAM per channel: 12 words (POS, CTL, 8 data words, terminator×2)
- `UpdateSpriteData()` fills per-pair chip-RAM data

### Sprite Palette Colors
- Paired sprite: even data = plane 0, odd data = plane 1
- Pixel encoding: 00=transparent, 01=body, 10=accent, 11=overlay
- Color registers: `COLOR(17+4n)` for body, `COLOR(18+4n)` for accent, `COLOR(19+4n)` for overlay, where n = pair index (0-3)

### VBlank Handler
- Sets SPRxPTH/PTL for all 8 channels — chip RAM data (POS/CTL) controls position
- Does NOT write SPRxPOS/CTL registers (would override chip RAM VSTART)

### DMA Trigger (chip RAM POS/CTL drives positioning)
1. Init sets SPRxPOS/CTL to VSTART=300 (off-screen)
2. At scanline 300 of frame 1: DMA matches VSTART=300, reads POS+CTL from chip RAM
3. Chip RAM for used pairs has correct VSTART (e.g., 150) → register updated
4. Frame 2 onwards: DMA triggers at the correct VSTART from chip RAM
5. 8 data words read (one per scanline), then VSTOP match or terminator stops DMA
6. VBlank handler only updates SPRxPTH/PTL — POS/CTL carry over from chip RAM load

### Palette (Sprite Colors)
- Formula: Pair `n` → `COLOR(17+4n)` for pixel 01 (body), `COLOR(18+4n)` for pixel 10 (accent), `COLOR(19+4n)` for pixel 11 (overlay)
- White (pairs 0-1): body=0xFFF, accent=0x0CF
- Black (pairs 2-3): body=0x000, accent=0xF00
- Runtime alternation flips body↔accent per frame for visual effect
- `g_Palette[32]` (indices 0-31); copper writes COLOR00-31 each frame

### Slot Assignment
- Each shot iterates available pairs, assigns first free pair of any polarity
- Rejects `x > 319` (off-screen)
- Max 4 sprinstances (one per pair)
- Remaining shots fall back to blitter `DrawBob16_2bpl`

### Constraints
- HSTART = 129 + screen_x; `screen_x > 126` wraps (8-bit)
- VSTOP in chip RAM CTL = `(vstart + SHOT_H) & 0x0F` (only 4 bits on OCS)
- Copper NEVER touches sprite registers (DMA overrides at VSTART trigger)

## Key Files
- **main.c**: game loop, VBlank handler, UpdateSpriteData, copper builder
- **gfx.h**: shot sprite bitmaps (g_Shot_Body[8], g_Shot_Accent[8]), SHOT_SPR_WORDS=12
- **blitter.s**: DrawBob16d2Asm, DrawForceFieldMaskAsm, DrawForceFieldMask2Asm assembly blitter routines, ClearGameAreaAsm (blitter D-zero-fill)
- **nau_dx.h**: SHOT_H=8, SHOT_W=4, MAX_SHOTS=24
