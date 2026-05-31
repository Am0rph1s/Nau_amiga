# Nau DX Amiga - Estat del Projecte

## Canvis implementats

### 1. Onades d'enemics (`main.c`)
- `SpawnEnemy()`, `SpawnBoss()`, `SpawnExplosion()` (~linia 447-530)
- 25 nivells amb mascares de tipus d'enemic i flags de boss
- 5 biomes (paletes de color) que canvien cada 5 nivells
- Col·lisió amb puntuació per tipus d'enemic
- Moviment del boss: entra des de dalt, es manté a `BOSS_HOLD_Y`, oscil·la

### 2. Double buffer (`main.c:691-697`)
- 2 buffers de pantalla (51200 bytes × 2 = 100KB chip RAM)
- Swap al VBlank, render off-screen → elimina tearing
- `draw_buf` (off-screen) i `show_buf` (visible) s'intercanvien cada frame

### 3. Velocitats (`nau_dx.h`)
| Constant | Abans | Ara |
|----------|-------|-----|
| SHIP_SPEED_X/Y | 5 | 8 |
| SHOT_SPEED | 5 | 7 |
| FIRE_COOLDOWN | 4 | 3 |
| ENEMY_SPEED_BASIC | 2 | 3 |
| ENEMY_SPEED_FAST | 3 | 4 |
| ENEMY_SPEED_HEAVY | 1 | 2 |
| ENEMY_SPEED_DIVER | 3 | 5 |
| ENEMY_SPEED_BOSS | 1 | 2 |

---

## Problema principal: Joystick digital al Port 2

### Maquinari
- Joystick digital Atari al **Port 2** (esquerra des del darrere de l'Amiga)
- Hardware Port 1 → `JOY1DAT` (0xDFF00C)
- Fire: `CIAAPRA` bit 7
- Els 4 pins de direcció van al comptador de mouse del CIA (8520)

### Problema fonamental
Els pins del joystick al Port 2 van connectats al **comptador del CIA**, no a GPIOs simples. El comptador només detecta **transicions** (flanques), no l'estat continu (nivell).

Per a un joystick digital (polsadors):
- Prémer = 1 transició → comptador +1
- Mantenir = 0 transicions → comptador no canvia
- Deixar anar = 1 transició → comptador -1

Això vol dir que el comptador no pot indicar si el joystick està premut o no en un moment donat. Només detecta el moment de prémer/deixar anar.

### Solució actual: Acumulador (`ReadJoy()`)
Cada transició suma/resta 1 a un acumulador. El release genera la transició oposada i el torna a 0.

```c
acc_x += dx;  // dx = delta del comptador horitzontal (8-bit)
acc_y += dy;  // dy = delta del comptador vertical (8-bit)
```

Avantatge: sense latches, timeouts ni cooldowns. El release cancel·la naturalment el press.

### Mapeig d'eixos (empíric)
| Direcció | Eix | Signe |
|----------|-----|-------|
| UP | dy | > 0 |
| LEFT | dy | < 0 |
| DOWN | dx | > 0 |
| RIGHT | dx | < 0 (predicció, no confirmat) |

## Problemes restants

### 1. Mapeig d'eixos ambigu
UP i LEFT comparteixen el mateix eix del comptador (`dy`). RIGHT podria estar a `dx < 0` o també a `dy > 0`. Si RIGHT i UP comparteixen eix (`dy > 0`), és impossible distingir-los només amb el comptador → les diagonals UP+RIGHT fallen.

### 2. Direcció "enganxada" en diagonals
En diagonals, si dues direccions comparteixen eix, els releases poden no cancel·lar-se correctament. L'acumulador pot quedar-se amb un valor residual ≠ 0 i la nau segueix movent-se.

### 3. POT / CIABPRB no responen
Ni la línia POT (Paula, `pot & 0x0400`/`0x4000`) ni el `CIABPRB` (CIA odd, `0xBFD100`) han respost a les proves:
- POT: possiblement necessita configuració prèvia que `TakeSystem()` no proporciona
- CIABPRB: possiblement requereix configurar `DDRB` (0xBFD300) o el WinUAE no ho emula

### 4. Parpelleig de la nau
La nau parpelleja (els enemics no). El double buffer hauria d'eliminar el tearing general. Possible causa: interferència del `ReadJoy()` amb el chipset (quan s'escrivia a `POTGO`/`DDRB` cada frame). Amb la versió actual (només lectures del comptador) podria estar resolt.

## Propers passos suggerits

1. **Investigar codi font de jocs reals d'Amiga** que llegeixin joystick digital bare-metal (no suposicions ni documentació teòrica)
2. **Provar amb `lowlevel.library` o `gameport.device`** enlloc de hardware directe (requereix obrir la llibreria abans de `TakeSystem()`)
3. **Acceptar la limitació** i usar 3 direccions via comptador + 1 via un altre mètode, o limitar el joc a 3 direccions
