#pragma once

// ============================================================================
// NAU DX AMIGA - Constants & Data Structures
// Amiga 500 / OCS - 320x256 lowres, 5 bitplanes (32 colors)
// ============================================================================

// --- Screen layout ----------------------------------------------------------
#define SCREEN_W        320
#define SCREEN_H        256
#define SCREEN_BPL      6       // 6 bitplanes -> dual-playfield 3+3 (8+8 colors)

// Layout: [wall_L 16px][game 256px][wall_R 16px][HUD 32px]
#define WALL_L_X        0
#define WALL_L_W        16
#define GAME_X0         16
#define GAME_X1         272
#define GAME_W          256
#define GAME_Y0         0
#define GAME_H          256
#define HUD_X           272
#define HUD_W           48

// --- Player ship (Animated 32x24) -------------------------------------------
#define SHIP_W          32
#define SHIP_H          24
#define SHIP_SPEED_X    8
#define SHIP_SPEED_Y    8
#define SHIP_MIN_X      16
#define SHIP_MAX_X      (SCREEN_W - SHIP_W - 16)
#define SHIP_MIN_Y      8
#define SHIP_MAX_Y      (GAME_H - SHIP_H - 8)
#define SHIP_SPAWN_X    (GAME_X0 + GAME_W/2 - SHIP_W/2)
#define SHIP_SPAWN_Y    (GAME_H - SHIP_H - 32)
#define SHIP_HIT_OX     6
#define SHIP_HIT_OY     6
#define SHIP_HIT_W      (SHIP_W - 12)
#define SHIP_HIT_H      (SHIP_H - 12)

// --- NEW: Xenon 2 style ship (32x24) ----------------------------------------
#define SHIP_NEW_W      32
#define SHIP_NEW_H      24
#define SHIP_NEW_WW     2       // width in words

// --- NEW: Multi-size enemies (Xenon 2 style) -------------------------------
#define ENEMY_SMALL_W   16
#define ENEMY_SMALL_H   16
#define ENEMY_MEDIUM_W  24
#define ENEMY_MEDIUM_H  24
#define ENEMY_LARGE_W   32
#define ENEMY_LARGE_H   32

// --- Player shots -----------------------------------------------------------
#define MAX_SHOTS       4
#define SHOT_SPEED      7
#define SHOT_W          4
#define SHOT_H          8
#define FIRE_COOLDOWN   3
#define POLARITY_HOLD_FRAMES  8  // frames of holding fire to switch to black polarity

// --- Enemy types ------------------------------------------------------------
#define ENEMY_TYPE_BASIC    0
#define ENEMY_TYPE_FAST     1
#define ENEMY_TYPE_BOSS     5

#define ENEMY_SPEED_BASIC   3
#define ENEMY_SPEED_FAST    4
#define ENEMY_SPEED_BOSS    2

#define ENEMY_SCORE_BASIC   10
#define ENEMY_SCORE_FAST    20
#define ENEMY_SCORE_BOSS    1500

#define ENEMY_W             24
#define ENEMY_H             24
#define ENEMY_BOSS_W        32
#define ENEMY_BOSS_H        24
#define MAX_ENEMIES         6

// --- Movement patterns ------------------------------------------------------
#define PATT_STRAIGHT   0
#define PATT_ZIGZAG     1
#define PATT_DIAGONAL   2

// --- Enemy shots ------------------------------------------------------------
#define MAX_ENEMY_SHOTS     12
#define ENEMYSHOT_SPEED_Y   5
#define ENEMYSHOT_COOLDOWN  18
#define ENEMYSHOT_STAGGER   5
#define ENEMYSHOT_W         4
#define ENEMYSHOT_H         4

// --- Explosions -------------------------------------------------------------
#define MAX_EXPLOSIONS      6
#define EXP_FRAMES          4
#define EXP_KIND_ENEMY      0
#define EXP_KIND_SHIP       1
#define EXP_KIND_BOSS       2

// --- Wave system ------------------------------------------------------------
#define WAVE_PLAN_MAX       8
#define SPAWN_FIRST_DELAY   4
#define SERIAL_DELAY        8
#define ENDGAME_FINAL_LEVEL 25
#define EXTRA_LIFE_EVERY    5000

#define LMASK_BASIC     (1<<0)
#define LMASK_FAST      (1<<1)
#define LCFG_F_BOSS1    1
#define LCFG_F_BOSS2    2

// --- Boss -------------------------------------------------------------------
#define BOSS_HP_BASE        15
#define BOSS_HP_PER_TIER    5
#define BOSS_HOLD_Y         40
#define BOSS_Y_OSC          20
#define BOSS_TIERS_MAX      5

// --- Starfield --------------------------------------------------------------
#define N_STARS_1       10  // slow  - dim
#define N_STARS_2       12  // med   - medium
#define N_STARS_3       14  // fast  - bright

// --- Game states ------------------------------------------------------------
#define GS_TITLE        0
#define GS_PLAYING      1
#define GS_GAMEOVER     2
#define GS_WIN          3

#define TS_MENU         0
#define TS_HISCORE      1
#define TS_HELP         2
#define TS_ATTRACT      3

// --- Respawn ----------------------------------------------------------------
#define SHIP_EXPL_TIMER     12

// --- Biomes -----------------------------------------------------------------
#define BIOME_COUNT     5

// --- HiScore ----------------------------------------------------------------
#define HISCORE_COUNT   3

// ============================================================================
// DATA STRUCTURES
// ============================================================================

typedef struct {
    short x, y;
} TStar;

typedef struct {
    short  x, y;
    short  active;
    short  variant;     // 0=white (default), 1=black (charged)
} TShot;

typedef struct {
    short  x, y;
    short  active;
    short  type;
    short  health;
    short  fire_cd;
    short  vx, vy;
    short  pattern;
    short  zig_timer;
    short  boss_hp_max;
    short  boss_vosc;
    short  variant;     // 0=normal, 1=inverted
} TEnemy;

typedef struct {
    short  x, y;
    short  active;
    short  vx, vy;
    short  variant;     // 0=white (safe), 1=black (damages)
} TEnemyShot;

typedef struct {
    short  x, y;
    short  active;
    short  frame;
    short  kind;
} TExplosion;

typedef struct {
    unsigned short score;
    unsigned char  level;
    char           name[4]; // 3 chars + null
} THiScore;

typedef struct {
    unsigned char waves;
    unsigned char per_wave;
    unsigned char mask;
    unsigned char flags;
} TLevelConfig;
