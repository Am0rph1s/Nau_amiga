| entities.s - 68k ASM update routines for game entities
| Replaces the per-entity update loops in main.c with hand-tuned ASM.
| All routines use D0/D1/A0/A1 as scratch, no parameter passing
| (operate on globals directly via PC-relative LEA).
|
| Offsets and sizes MUST match the C struct definitions in nau_dx.h:
|   TShot:       x=0, y=2, active=4, variant=6          size=8
|   TEnemy:      x=0, y=2, active=4, type=6, health=8, fire_cd=10, vx=12, vy=14,
|                pattern=16, zig_timer=18, boss_hp_max=20, boss_vosc=22,
|                variant=24                               size=26
|   TEnemyShot:  x=0, y=2, active=4, vx=6, vy=8, variant=10  size=12
|   TExplosion:  x=0, y=2, active=4, frame=6, kind=8        size=10

        .equ    TSHOT_SIZE,        8
        .equ    TSHOT_X,           0
        .equ    TSHOT_Y,           2
        .equ    TSHOT_ACTIVE,      4

        .equ    TENEMY_SIZE,       26
        .equ    TENEMY_X,          0
        .equ    TENEMY_Y,          2
        .equ    TENEMY_ACTIVE,     4
        .equ    TENEMY_TYPE,       6
        .equ    TENEMY_HEALTH,     8
        .equ    TENEMY_VY,        14
        .equ    TENEMY_VARIANT,   24

        .equ    TES_SIZE,         12
        .equ    TES_X,             0
        .equ    TES_Y,             2
        .equ    TES_ACTIVE,        4
        .equ    TES_VARIANT,      10

        .equ    EXP_SIZE,         10
        .equ    EXP_X,             0
        .equ    EXP_Y,             2
        .equ    EXP_ACTIVE,        4
        .equ    EXP_FRAME,         6

        .equ    SHOT_SPEED,        7
        .equ    GAME_Y0,           0
        .equ    GAME_H,          256
        .equ    SCREEN_H,        256
        .equ    ENEMYSHOT_SPEED_Y, 5
        .equ    ENEMY_W_HALF,     12
        .equ    ENEMY_H,         24
        .equ    MAX_SHOTS,         4
        .equ    MAX_ENEMIES,       6
        .equ    MAX_ENEMY_SHOTS,  12
        .equ    MAX_EXPLOSIONS,    6
        .equ    EXP_FRAMES,        4
        .equ    EXP_FRAMES_TOTAL, 16

        .text

| ============================================================
| void AsmUpdatePlayerShots(void)
| For each active player shot: y -= SHOT_SPEED; if y < GAME_Y0, active=0.
| ============================================================
        .global AsmUpdatePlayerShots
AsmUpdatePlayerShots:
        lea     g_Shots,a0
        move.w  #MAX_SHOTS-1,d0
.ups_loop:
        tst.w   TSHOT_ACTIVE(a0)
        beq.s   .ups_skip
        subq.w  #SHOT_SPEED,TSHOT_Y(a0)
        cmpi.w  #GAME_Y0,TSHOT_Y(a0)
        bge.s   .ups_skip
        clr.w   TSHOT_ACTIVE(a0)
.ups_skip:
        adda.w  #TSHOT_SIZE,a0
        dbra    d0,.ups_loop
        rts

| ============================================================
| void AsmUpdateEnemyShots(void)
| For each active enemy shot: y += ENEMYSHOT_SPEED_Y; if y >= SCREEN_H, active=0.
| ============================================================
        .global AsmUpdateEnemyShots
AsmUpdateEnemyShots:
        lea     g_EnemyShots,a0
        move.w  #MAX_ENEMY_SHOTS-1,d0
.ues_loop:
        tst.w   TES_ACTIVE(a0)
        beq.s   .ues_skip
        addq.w  #ENEMYSHOT_SPEED_Y,TES_Y(a0)
        cmpi.w  #SCREEN_H,TES_Y(a0)
        blt.s   .ues_skip
        clr.w   TES_ACTIVE(a0)
.ues_skip:
        adda.w  #TES_SIZE,a0
        dbra    d0,.ues_loop
        rts

| ============================================================
| void AsmUpdateEnemies(void)
| For each active enemy: y += vy; if y > GAME_H, active=0 and g_WaveKilled++.
| ============================================================
        .global AsmUpdateEnemies
AsmUpdateEnemies:
        lea     g_Enemies,a0
        move.w  #MAX_ENEMIES-1,d0
.ue_loop:
        tst.w   TENEMY_ACTIVE(a0)
        beq.s   .ue_skip
        move.w  TENEMY_VY(a0),d1
        add.w   d1,TENEMY_Y(a0)
        cmpi.w  #GAME_H,TENEMY_Y(a0)
        ble.s   .ue_skip
        clr.w   TENEMY_ACTIVE(a0)
        addq.w  #1,g_WaveKilled
.ue_skip:
        adda.w  #TENEMY_SIZE,a0
        dbra    d0,.ue_loop
        rts

| ============================================================
| void AsmUpdateExplosions(void)
| For each active explosion: frame++; if frame >= EXP_FRAMES*4, active=0.
| ============================================================
        .global AsmUpdateExplosions
AsmUpdateExplosions:
        lea     g_Explosions,a0
        move.w  #MAX_EXPLOSIONS-1,d0
.uxp_loop:
        tst.w   EXP_ACTIVE(a0)
        beq.s   .uxp_skip
        addq.w  #1,EXP_FRAME(a0)
        cmpi.w  #EXP_FRAMES_TOTAL,EXP_FRAME(a0)
        blt.s   .uxp_skip
        clr.w   EXP_ACTIVE(a0)
.uxp_skip:
        adda.w  #EXP_SIZE,a0
        dbra    d0,.uxp_loop
        rts

| ============================================================
| void AsmEnemyFire(void)
| For each active enemy: if (g_FrameCounter + i*7) % 30 == 0,
| spawn an enemy shot at (e->x + ENEMY_W/2, e->y + ENEMY_H) with e->variant.
|
| Modulo computed by repeated subtraction (subq + dbra) instead of divs.
| divs.w has the quotient/remainder split (lo=quotient, hi=remainder)
| which is error-prone near zero; the sub loop is 100% reliable.
| Max iterations: 2185 (any 16-bit value / 30 fits in 2185 subtractions).
| ============================================================
        .global AsmEnemyFire
AsmEnemyFire:
        lea     g_Enemies,a0
        move.w  #MAX_ENEMIES-1,d0
        clr.w   d1
.aef_outer:
        tst.w   TENEMY_ACTIVE(a0)
        beq.s   .aef_next

        | Compute d2 = (g_FrameCounter + i*7) as a 16-bit value
        | (d2 high word stays 0 because i*7 is in 0..35, always positive)
        move.w  d1,d2
        lsl.w   #3,d2
        sub.w   d1,d2
        add.w   g_FrameCounter,d2

        | Compute d2 = d2 mod 30 by subtracting 30 until d2 < 30.
        | d3 is the loop counter: starts at 2185 (max 16-bit value / 30 + slack).
        | If the subi borrows (carry clear, d2 went negative), add 30 back
        | to restore the remainder and we're done.
        move.w  #2185,d3
.aef_mod:
        subi.w  #30,d2
        bcc.s   .aef_mod_cont
        addi.w  #30,d2           | d2 went < 0, restore remainder
        bra.s   .aef_mod_done
.aef_mod_cont:
        dbra    d3,.aef_mod
.aef_mod_done:
        | d2 is now the remainder in [0, 29]
        tst.w   d2
        bne.s   .aef_next

        | Spawn enemy shot: find first inactive slot
        lea     g_EnemyShots,a1
        move.w  #MAX_ENEMY_SHOTS-1,d2
.aef_find:
        tst.w   TES_ACTIVE(a1)
        beq.s   .aef_do_spawn
        adda.w  #TES_SIZE,a1
        dbra    d2,.aef_find
        bra.s   .aef_next
.aef_do_spawn:
        move.w  #1,TES_ACTIVE(a1)
        move.w  TENEMY_X(a0),d2
        ext.l   d2
        addi.w  #ENEMY_W_HALF,d2
        move.w  d2,TES_X(a1)
        move.w  TENEMY_Y(a0),d2
        ext.l   d2
        addi.w  #ENEMY_H,d2
        move.w  d2,TES_Y(a1)
        move.w  TENEMY_VARIANT(a0),TES_VARIANT(a1)
.aef_next:
        addq.w  #1,d1
        adda.w  #TENEMY_SIZE,a0
        dbra    d0,.aef_outer
        rts
