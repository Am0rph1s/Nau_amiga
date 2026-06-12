# Progress: Ikaruga-style 2D Shooter for Amiga

## Concept

A vertical-scrolling shoot-'em-up for OCS Amiga inspired by **Ikaruga**'s
core mechanic: the player ship has a **polarity** (white or black) and can
absorb enemy shots of the same polarity. Toggling polarity is the main
defensive action; chain absorptions let you unleash dense counter-fire.

Target hardware: OCS Amiga (500/600/1200) — 68000 CPU, 512 KB Chip RAM,
kickstart 1.3+.

## Status

- Engine compiles with `m68k-amiga-elf-gcc -Ofast -flto -fwhole-program`.
- Builds to `out/nau_dx.exe` (Amiga Hunk executable).
- Force field (banded-dither bubble + polarity-sweep transition) renders.
- Player ship renders with dual-polarity greyscale.
- BASIC and FAST enemies spawn in waves and shoot downward.
- Collision detection (shot-vs-enemy, enemy-vs-ship, shot-vs-ship) works.
- **5 entity update loops moved to 68k ASM** (`entities.s`).
- Wave/level progression, scoring, extra life, game-over screen.

## Build & Toolchain

- **Compiler**: `m68k-amiga-elf-gcc` (m68k-amiga-elf target, `-m68000`)
- **Assembler**: `m68k-amiga-elf-as` (GNU AS, m68k syntax)
- **Toolchain location (Windows)**:
  `C:\Users\esquerra\.devin\extensions\bartmanabyss.amiga-debug-1.8.2\bin\win32\opt\bin\`
  (also at `win32/gnumake.exe`)
- **Build command**: `gnumake` from project root.
- **Output**: `out/nau_dx.elf` → `out/nau_dx.exe` (Hunk-exe).

### Compiler flags (`Makefile`)
- `-Ofast -m68000` — full optimization for 68000 (no 020+/FPU insns)
- `-flto -fwhole-program` — link-time + whole-program optimization
- `-fno-exceptions -ffunction-sections -fdata-sections` — slim output
- `-fomit-frame-pointer -fno-tree-loop-distribution` — better ASM
- `-nostdlib` — no libc, custom startup in `gcc8_c_support.c`

## File Map

| File | Role |
|------|------|
| `main.c` | Game loop, render, logic, palette, copper list |
| `entities.s` | 68k ASM entity update routines |
| `blitter.s` | 68k ASM blitter routines (clear, parallax, bobs) |
| `blitter.h` | Declarations for ASM + C interop |
| `nau_dx.h` | Constants, structs (`TShot`, `TEnemy`, etc.), globals |
| `gfx.h`, `gfx_ship_white.h`, `gfx_enemy_*.h` | Sprite data (4-bitplane, etc.) |
| `support/gcc8_c_support.c` | Startup code, KPrintF, debug helpers |
| `support/gcc8_a_support.s` | 68000 `__divsi3`, `__mulsi3` |
| `tools/ship_import_dual.py` | Convert ship PNGs to C arrays |
| `tools/gen_inverted.py` | Generate black-polarity enemy variants |
| `progress.md` | This file |

## Hardware / Driver Setup

### Dual-playfield (OCS)
- `BPLCON0` bit 10 (DBLPF) = 1 → dual-playfield mode
- 6 bitplanes: BPL1..3 = Playfield 1 (background/walls), BPL4..6 = Playfield 2 (game)
- `BPLCON2` bit 6 (PF2PRI) = 1 → PF2 draws on top of PF1
- Copper list interleaves plane pointers: `BPL1=PF1_0, BPL2=PF2_0, BPL3=PF1_1,
  BPL4=PF2_1, BPL5=PF1_2, BPL6=PF2_2`

### Sprite DMA
- **All hardware sprites abandoned**. In OCS dual-playfield mode, hardware
  sprites render behind both playfields — useless for game sprites.
- Player/enemy shots are CPU-drawn into BPL1/BPL3 (PF2).

### Color palette (32 colors)
| Range | Purpose |
|-------|---------|
| 0-7   | PF1 (background, walls) — `0x0321` dark, `0x0654` light |
| 8     | PF2 transparent (shows PF1 behind) |
| 9-11  | PF2 greyscale (dark, mid, light) |
| 12    | **PF2 force-field source** (g_Palette[10]) |
| 13    | **PF2 force-field dest** (g_Palette[12]) |
| 14-15 | Reserved accents (yellow, orange — unused for now) |

The Copper reloads colors from `g_Palette[]` every frame via
`BuildCopperListEx`. **Direct writes to `custom->color[]` are silently
overwritten** by the Copper — always modify `g_Palette[]`, not the
registers.

## Force Field (current)

### Bubble
- 1-bit **banded-dither** circular disc (r=22, 48×48) drawn into BPL6.
- Filled with a 4×4 Bayer threshold pattern at ~50% density → background
  shows through the unset pixels (translucent "dome" without true alpha).
- The ship's draw still clears BPL6 in its bounding box, so the visible
  result is the banded-dither annulus around the ship.
- Bubble scrolls vertically through 4 frames
  (`g_FFBubScrollSet[0..3]`) at 6.25 fps — `scroll = (g_FrameCounter >> 3) & 3`.

### Polarity transition
- 4-frame window when polarity flips (player holds fire for 8 frames).
- Two mask sets: `g_FFSweepSrcSet[0..3]` and `g_FFSweepDstSet[0..3]`,
  each is a solid disc split by a vertical wiper at x=2, 16, 30, 44.
- BPL4 = source color (old polarity), BPL6 = destination (new polarity).
- Wiper direction:
  - white → black: L→R, BPL4=cyan, BPL6=red
  - black → white: R→L, BPL4=red, BPL6=cyan
- Drawn AFTER the ship so the dome covers it.

### Polarity colors
- White: `0x0CF` (light blue) ↔ `0x0FF` (cyan)
- Black: `0xA00` (dark red) ↔ `0xF00` (red)
- Player ship + player shots + game sprites use the corresponding color.
- Enemy shots use their variant's color (0=white, 1=black).

## Player Ship

- Position: `g_ShipX`, `g_ShipY` (16-bit, signed).
- Bounds: `SHIP_MIN_X..SHIP_MAX_X`, `SHIP_MIN_Y..SHIP_MAX_Y`.
- Speed: `SHIP_SPEED_X` horizontal, `SHIP_SPEED_Y` vertical.
- Sprite: 32×24, 4-bitplane greyscale. Two data sets (A and B) in
  `gfx_ship_white.h` — A is the resting ship, B is "fire held" ship.
- Render: `DrawShipAnim` merges 4 BPLs into 2 BPLs for PF2:
  - `planes 0|1 → BPL1` (PF2 bit 0)
  - `planes 2|3 → BPL3` (PF2 bit 1)
- White polarity: `BPL1 = (dataLo & ~dataHi)`, `BPL3 = (dataLo | dataHi)` —
  "both" pixels (where both planes are set) are excluded → cleaner shape.
- Black polarity: inverted (`planes 0|1 → BPL3`, `2|3 → BPL1`).
- Hitbox: `SHIP_HIT_OX=6, SHIP_HIT_OY=6, SHIP_HIT_W=20, SHIP_HIT_H=12`
  (smaller than the visible 32×24 sprite).

## Enemy System

### Types (in `nau_dx.h`)
- `ENEMY_TYPE_BASIC` (24×24 sprite) — health 1, slow.
- `ENEMY_TYPE_FAST` (16×16 sprite) — health 1, fast.
- HEAVY/DIVER/BOMBER removed in current scope (only BASIC + FAST).

### Variants (white/black polarity)
- `gfx_enemy_basic24.h` (white) + `gfx_enemy_basic24_inv.h` (black)
- `gfx_enemy_fast16.h` (white) + `gfx_enemy_fast16_inv.h` (black)
- Inverted version: swap BPL1↔BPL3 (greyscale values 1↔2 exchanged).
- Variant assigned at spawn: `(g_FrameCounter + g_WaveSpawned) & 1`.

### Spawn
- `g_WaveActive` = 1 while wave in progress.
- `g_WaveTotal` = waves × per-wave (level-dependent).
- First enemy after `SPAWN_FIRST_DELAY` frames, then every `SERIAL_DELAY`.
- `SpawnEnemy(type)`: position, vy, variant, health, pattern.

### Rendering
- 2-bitplane draw: `DrawBob16d2Asm` or `DrawBob32d2Asm` depending on size.
- Selected by `g_EnemyColor[]` array per type (1, 2, 3 → different greys).

## Shots

### Player shots (`TShot`)
- `MAX_SHOTS = 4` (will be raised to 16-24 for absorption).
- Speed: `SHOT_SPEED = 7` px/frame upward.
- Variant: 0 (white) or 1 (black), set at fire time = `g_ShipPolarity`.
- Hitbox: 4×8 px.

### Enemy shots (`TEnemyShot`)
- `MAX_ENEMY_SHOTS = 12`.
- Speed: `ENEMYSHOT_SPEED_Y = 5` px/frame downward.
- Variant: copied from enemy at spawn.

### Absorption (planned, not yet implemented)
- When player polarity == enemy shot polarity AND shot is close to ship:
  absorb shot → convert to player shot (variant = player polarity).
- Requires `MAX_SHOTS` increase.

## Collisions (in C, after ASM updates)

- **Shot vs enemy**: AABB test on each pair. Hit → shot inactive, enemy
  health -1. If health ≤ 0: enemy inactive, score += type-dependent,
  `g_WaveKilled++`, spawn explosion, check extra-life.
- **Enemy vs ship**: AABB test, only if not `g_ShipExploding`. Hit → both
  inactive, spawn explosion on ship, set `g_ShipExploding=1`,
  `g_ShipExplTimer=SHIP_EXPL_TIMER`, `g_Lives--`.
- **Enemy shot vs ship**: AABB test, only if not exploding AND
  `g_EnemyShots[i].variant != g_ShipPolarity` (same polarity = no damage).

## Explosions

- `MAX_EXPLOSIONS = 6`.
- `EXP_KIND_ENEMY=0`, `EXP_KIND_SHIP=1`, `EXP_KIND_BOSS=2`.
- Frame counter ticks per frame; despawn at `EXP_FRAMES*4 = 16` frames.

## Game State

- `GS_TITLE` (0): star animation, wait for fire → `GS_PLAYING`.
- `GS_PLAYING` (1): gameplay loop.
- `GS_GAMEOVER` (2): wait for fire → `GS_TITLE`.
- `GS_WIN` (3): after `ENDGAME_FINAL_LEVEL`.

### Wave/level
- `g_Level` (1..ENDGAME_FINAL_LEVEL), `g_CurrentBiome = (g_Level-1)/5 % BIOME_COUNT`.
- Per-level config: `g_Levels[]` array of `{mask, waves, per_wave}`.
- `g_Score`, `g_Lives` (init 3), `g_NextLifeAt` (extra life every 5000).

## Polarity Logic

- Default polarity: 0 (white).
- Player holds fire for `POLARITY_HOLD_FRAMES = 8` → polarity flips to 1 (black).
  - `g_ForceFieldTransition = 4` (4-frame sweep animation).
  - `g_ForceFieldSweepDir = 0` (white→black).
- Player releases fire after being in black → polarity flips back to 0.
  - `g_ForceFieldTransition = 4`.
  - `g_ForceFieldSweepDir = 1` (black→white).
- Decrement `g_ForceFieldTransition` per frame; when 0, back to normal.
- Player shots inherit current polarity at fire time.

## 68k ASM Entity Update Routines (`entities.s`)

Replaces 5 C update loops in `main.c` with hand-tuned ASM. Keeps LTO
whole-program optimizations while exposing the entity arrays to the
assembler.

### Routines

| Routine | Replaces | C lines | ASM size | Behavior |
|---------|----------|---------|----------|----------|
| `AsmUpdatePlayerShots` | `main.c:1994-1998` | 5 | 40 B | y -= SHOT_SPEED, despawn if y < 0 |
| `AsmUpdateEnemyShots` | `main.c:2048-2050` | 3 | 40 B | y += ENEMYSHOT_SPEED_Y, despawn if y ≥ 256 |
| `AsmUpdateEnemies` | `main.c:1997` | 1 | 50 B | y += vy, despawn if y > 256, g_WaveKilled++ |
| `AsmUpdateExplosions` | `main.c:2073` | 1 | 40 B | frame++, despawn if frame ≥ 16 |
| `AsmEnemyFire` | `main.c:2070` | 1 | 80 B | (g_FrameCounter + i*7) % 30 == 0 → spawn shot |

Total: ~250 bytes. Each uses `dbra` for the loop, `tst.w` for the active
check, `adda.w #N,a0` for the next-element stride.

### Calling convention
- ASM routines do not save any registers — they use D0/D1/A0/A1 only
  (scratch) and don't touch A2-A6/D2-D7. The C compiler reloads any
  spilled values across the call.
- No parameter passing: routines operate on globals directly via
  PC-relative `lea g_Foo,a0`.
- Returns void.

### Build system
- `entities.s` added to `s_sources` in `Makefile`.
- `blitter.h` declares the 5 externs.
- `main.c` calls them in the update phase, between the C collision code.

## Build System Workarounds

### `externally_visible` for `-fwhole-program`
- `-fwhole-program` makes all symbols internal (hidden). Without
  external visibility, the linker can't see `g_Shots`, `g_Enemies`, etc.
  from the ASM file.
- Solution: mark the 6 globals with `__attribute__((externally_visible))`:
  ```c
  __attribute__((externally_visible)) volatile short g_FrameCounter = 0;
  __attribute__((externally_visible)) TShot      g_Shots[MAX_SHOTS];
  __attribute__((externally_visible)) TEnemy     g_Enemies[MAX_ENEMIES];
  __attribute__((externally_visible)) TEnemyShot g_EnemyShots[MAX_ENEMY_SHOTS];
  __attribute__((externally_visible)) TExplosion g_Explosions[MAX_EXPLOSIONS];
  __attribute__((externally_visible)) short g_WaveKilled = 0;
  ```
- This preserves `-fwhole-program` (full LTO) while exposing the
  necessary globals.

### Why NOT just remove `-fwhole-program`?
- First attempt: removed `-fwhole-program` to expose globals.
- **Broke the force-field animation** — `(g_FrameCounter >> 3) & 3` was
  evaluating wrong values, bubble looked like 2 frames instead of 4.
- **Broke enemy fire** — but this had a separate root cause (see below).
- Restoring `-fwhole-program` + `externally_visible` fixed the dome;
  the enemy fire bug persisted until the `divs` fix below.

## Bugs Found and Fixed

### Bug 1: `cmp.w #imm,mem` invalid encoding
- **Symptom**: assemble error "value of ffffff7c too large for field of
  1 byte".
- **Cause**: `cmp.w #GAME_Y0, TSHOT_Y(a0)` is not a valid M68K
  instruction. `CMP` only takes a register destination; for an
  immediate-to-memory comparison, `CMPI` is required.
- **Fix**: use `cmpi.w #GAME_Y0, TSHOT_Y(a0)` everywhere.

### Bug 2: wrong constants in initial ASM port
- **Symptom**: enemies off-screen by 1px or firing from wrong y.
- **Cause**: initial `entities.s` had `SHOT_SPEED=4`, `ENEMYSHOT_SPEED_Y=3`,
  `ENEMY_W/2=16`. Actual values (in `nau_dx.h`): `7`, `5`, `12`.
- **Fix**: corrected the `.equ` constants.

### Bug 3: duplicate labels
- **Symptom**: assemble error "symbol `.loop' is already defined".
- **Cause**: `.loop` and `.skip` are global labels in GAS M68K (not
  local). Using them in 5 different routines caused redefinitions.
- **Fix**: prefixed each label with the routine name (`.ups_loop`,
  `.ues_skip`, `.ue_loop`, `.uxp_skip`, `.aef_outer`, `.aef_find`,
  `.aef_do_spawn`, `.aef_next`).

### Bug 4: `divs.w` quotient vs remainder
- **Symptom**: enemies did not shoot at all.
- **Cause**: `divs.w #30,d2` puts the **quotient** in d2[0:15] (low word)
  and the **remainder** in d2[16:31] (high word). My initial code used
  `tst.w d2` directly, which tests the LOW word (quotient). When the
  modulo hits 0, the quotient is non-zero (e.g., 60/30 → quotient=2,
  remainder=0), so `tst.w` saw 2, Z=0, `bne` branched, shot was NOT
  spawned.
- **Fix**: insert `swap d2` before `tst.w` so the remainder is in the
  low word, then test that.
  ```asm
  divs    #30,d2           | lo=quotient, hi=remainder
  swap    d2               | move remainder to lo
  tst.w   d2               | test remainder
  bne.s   .aef_next
  ```
- **Symptom was subtle**: with `g_FrameCounter` very small (0-30), the
  remainder of `i*7 + g_FrameCounter` was always the same as the
  remainder in the C version. The bug only became visible for
  g_FrameCounter = 30, 60, 90, ... (every 30 frames) — exactly when
  enemies should fire. The C version (using 32-bit arithmetic with
  full 32-bit divs) and the 16-bit `i*7 + g_FrameCounter` modulus
  would diverge for the i≥1 cases at the right edge of the wrap.

### Bug 5: `-fwhole-program` hid symbols
- **Symptom**: linker errors `undefined reference to g_Shots` etc.
- **Fix**: `__attribute__((externally_visible))` on the 6 globals.

## Roadmap / TODO

### Phase 1: Polish current gameplay
- [ ] **Dual shot** (Ikaruga-style): the ship fires 2 shots, one per
  wing, straight (not angled). Reuse the same `g_ShotMask`. Increase
  `MAX_SHOTS` to 16-24 to host the extra shots.
- [ ] **Faster fire cadence**: reduce `FIRE_COOLDOWN` from 3 to 1-2.
- [ ] **Polarity absorption** (Ikaruga-style): when an enemy shot has
  the SAME polarity as the ship and is close, the ship absorbs it.
  The absorbed shot becomes a player shot added to the pool. This
  causes the screen to fill with shots quickly when polarity is right.
- [ ] **MAX_SHOTS increased** (4 → 16-24) to host absorbed shots.
- [ ] **Absorption visual feedback**: particles/glow on absorption,
  color matching the polarity.

### Phase 2: More content
- [ ] **Planet background image** for PF1 planes 0, 2, 4 (currently
  walls only, no background detail).
- [ ] **Level switching** between space (stars) and planet (background)
  modes.
- [ ] **More enemy types** (re-add HEAVY, DIVER, BOMBER with 2-BPL
  greyscale assets).
- [ ] **Boss fight** every 5 levels.

### Phase 3: Optimization
- [ ] Move more update loops to ASM (player shot vs enemy collision,
  enemy vs ship collision, enemy shot vs ship collision) once
  `MAX_SHOTS` is increased to 16-24 and the absorption is in place.
  These collisions are the hot path during dense fire.
- [ ] Consider `__builtin_expect` / branch hints in C for the collision
  checks.
- [ ] Profile with `KPrintF` to identify new hot spots.

## Useful references
- [Amiga Hardware Reference Manual](doc/Amiga_Hardware_Reference_Manual_3rd_edition.pdf)
- [Amiga Assembler Insider Guide (Paul Overaa, 1993)](https://amigasourcecodepreservation.gitlab.io/amiga-assembler-insider-guide/)
- m68k `divs.w` instruction manual: quotient in low word, **remainder in
  high word**. Always `swap d2` (or similar) to test the remainder.

## Toolchain notes
- `m68k-amiga-elf-gcc` 15.1.0
- `m68k-amiga-elf-as` GNU AS
- Target: 68000 (no 020+ / FPU / MMU)
- `elf2hunk` from VBCC toolchain converts the ELF to Amiga Hunk-exe.
