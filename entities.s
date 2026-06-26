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
.equ    MAX_SHOTS,         24
.equ    MAX_ENEMIES,       12
.equ    MAX_ENEMY_SHOTS,  32
.equ    MAX_EXPLOSIONS,    12
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

        | Compute d2 = d2 mod 30 using divu (unsigned divide).
        | divu.w #30,d3: d3 low word = quotient, d3 high word = remainder.
        | Swap high/low so d2 gets the remainder.
        clr.w   d3
        move.w  d2,d3
        divu.w  #30,d3
        swap    d3
        move.w  d3,d2
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

| ============================================================
| void AsmCollideShotsEnemies(void)
| ============================================================
        .global AsmCollideShotsEnemies
AsmCollideShotsEnemies:
        movem.l d2-d7/a2-a3,-(sp)

        lea     g_Enemies,a2       | a2 = enemy pointer
        move.w  #MAX_ENEMIES-1,d4  | d4 = enemy loop counter
.ase_enemy_loop:
        tst.w   TENEMY_ACTIVE(a2)
        beq     .ase_next_enemy

        lea     g_Shots,a3         | a3 = shot pointer
        move.w  #MAX_SHOTS-1,d5    | d5 = shot loop counter
.ase_shot_loop:
        tst.w   TSHOT_ACTIVE(a3)
        beq     .ase_next_shot

        | AABB Collision Check:
        | shot_x + SHOT_W (4) > enemy_x
        move.w  TSHOT_X(a3),d0
        addq.w  #4,d0
        cmp.w   TENEMY_X(a2),d0
        ble     .ase_next_shot

        | shot_x < enemy_x + ENEMY_W (24 or 48)
        move.w  TSHOT_X(a3),d0
        move.w  TENEMY_X(a2),d1
        move.w  TENEMY_TYPE(a2),d2
        cmpi.w  #2,d2              | ENEMY_TYPE_BIG is 2
        beq.s   .ase_x48
        add.w   #24,d1
        bra.s   .ase_check_x
.ase_x48:
        add.w   #48,d1
.ase_check_x:
        cmp.w   d1,d0
        bge     .ase_next_shot

        | shot_y + SHOT_H (16) > enemy_y
        move.w  TSHOT_Y(a3),d0
        add.w   #16,d0
        cmp.w   TENEMY_Y(a2),d0
        ble     .ase_next_shot

        | shot_y < enemy_y + ENEMY_H (24 or 48)
        move.w  TSHOT_Y(a3),d0
        move.w  TENEMY_Y(a2),d1
        move.w  TENEMY_TYPE(a2),d2
        cmpi.w  #2,d2              | ENEMY_TYPE_BIG is 2
        beq.s   .ase_y48
        add.w   #24,d1
        bra.s   .ase_check_y
.ase_y48:
        add.w   #48,d1
.ase_check_y:
        cmp.w   d1,d0
        bge     .ase_next_shot

        | COLLISION DETECTED!
        clr.w   TSHOT_ACTIVE(a3)   | g_Shots[s].active = 0
        subq.w  #1,TENEMY_HEALTH(a2)| e->health--
        bgt     .ase_next_shot     | If health > 0, enemy survives

        | Enemy dies!
        clr.w   TENEMY_ACTIVE(a2)  | e->active = 0
        addq.w  #1,g_WaveKilled

        | Score update: based on e->type
        move.w  TENEMY_TYPE(a2),d0
        cmpi.w  #1,d0              | ENEMY_TYPE_FAST is 1
        beq     .ase_score_fast
        add.w   #10,g_Score        | ENEMY_SCORE_BASIC is 10
        bra     .ase_score_done
.ase_score_fast:
        add.w   #20,g_Score        | ENEMY_SCORE_FAST is 20
.ase_score_done:

        | SpawnExplosion(e->x, e->y, EXP_KIND_ENEMY = 0)
        clr.l   d0
        move.l  d0,-(sp)           | kind = 0

        move.w  TENEMY_Y(a2),d0
        ext.l   d0
        move.l  d0,-(sp)           | y = e->y

        move.w  TENEMY_X(a2),d0
        ext.l   d0
        move.l  d0,-(sp)           | x = e->x

        jsr     SpawnExplosion
        lea     12(sp),sp          | restore stack (3 * 4 = 12 bytes)

        | Check extra life: if (g_Score >= g_NextLifeAt)
        move.w  g_Score,d0
        cmp.w   g_NextLifeAt,d0
        blo     .ase_extra_life_done

        addq.w  #1,g_Lives
        add.w   #5000,g_NextLifeAt | EXTRA_LIFE_EVERY is 5000
.ase_extra_life_done:

        | Since this enemy is dead, we break out of the shot loop
        bra     .ase_next_enemy

.ase_next_shot:
        adda.w  #TSHOT_SIZE,a3
        dbra    d5,.ase_shot_loop

.ase_next_enemy:
        adda.w  #TENEMY_SIZE,a2
        dbra    d4,.ase_enemy_loop

        movem.l (sp)+,d2-d7/a2-a3
        rts

| ============================================================
| void AsmCollideEnemiesShip(void)
| ============================================================
        .global AsmCollideEnemiesShip
AsmCollideEnemiesShip:
        tst.w   g_ShipExploding
        bne     .aces_done

        movem.l d2-d5/a2,-(sp)

        lea     g_Enemies,a2
        move.w  #MAX_ENEMIES-1,d4
.aces_loop:
        tst.w   TENEMY_ACTIVE(a2)
        beq     .aces_next

        | Collision check (AABB):
        | e->x + ENEMY_W (24) > g_ShipX + SHIP_HIT_OX (3)
        move.w  g_ShipX,d0
        addq.w  #3,d0             | g_ShipX + SHIP_HIT_OX
        move.w  TENEMY_X(a2),d1
        move.w  TENEMY_TYPE(a2),d2
        cmpi.w  #2,d2              | ENEMY_TYPE_BIG is 2
        beq.s   .aces_x48
        add.w   #24,d1
        bra.s   .aces_chk_x
.aces_x48:
        add.w   #48,d1
.aces_chk_x:
        cmp.w   d0,d1
        ble     .aces_next

        | e->x < g_ShipX + SHIP_HIT_OX + SHIP_HIT_W (3 + 12 = 15)
        move.w  TENEMY_X(a2),d1
        move.w  g_ShipX,d0
        add.w   #15,d0            | g_ShipX + SHIP_HIT_OX + SHIP_HIT_W
        cmp.w   d0,d1
        bge     .aces_next

        | e->y + ENEMY_H (24) > g_ShipY + SHIP_HIT_OY (6)
        move.w  g_ShipY,d0
        addq.w  #6,d0             | g_ShipY + SHIP_HIT_OY
        move.w  TENEMY_Y(a2),d1
        move.w  TENEMY_TYPE(a2),d2
        cmpi.w  #2,d2              | ENEMY_TYPE_BIG is 2
        beq.s   .aces_y48
        add.w   #24,d1
        bra.s   .aces_chk_y
.aces_y48:
        add.w   #48,d1
.aces_chk_y:
        cmp.w   d0,d1
        ble     .aces_next

        | e->y < g_ShipY + SHIP_HIT_OY + SHIP_HIT_H (6 + 12 = 18)
        move.w  TENEMY_Y(a2),d1
        move.w  g_ShipY,d0
        add.w   #18,d0            | g_ShipY + SHIP_HIT_OY + SHIP_HIT_H
        cmp.w   d0,d1
        bge     .aces_next

        | COLLISION DETECTED!
        clr.w   TENEMY_ACTIVE(a2)  | e->active = 0
        addq.w  #1,g_WaveKilled

        | SpawnExplosion(e->x, e->y, EXP_KIND_ENEMY = 0)
        clr.l   d0
        move.l  d0,-(sp)
        move.w  TENEMY_Y(a2),d0
        ext.l   d0
        move.l  d0,-(sp)
        move.w  TENEMY_X(a2),d0
        ext.l   d0
        move.l  d0,-(sp)
        jsr     SpawnExplosion
        lea     12(sp),sp

        | SpawnExplosion(g_ShipX, g_ShipY, EXP_KIND_SHIP = 1)
        move.w  #1,d0             | EXP_KIND_SHIP
        ext.l   d0
        move.l  d0,-(sp)
        move.w  g_ShipY,d0
        ext.l   d0
        move.l  d0,-(sp)
        move.w  g_ShipX,d0
        ext.l   d0
        move.l  d0,-(sp)
        jsr     SpawnExplosion
        lea     12(sp),sp

        | g_ShipExploding = 1
        move.w  #1,g_ShipExploding

        | g_ShipExplTimer = SHIP_EXPL_TIMER (12)
        move.w  #12,g_ShipExplTimer

        | g_Lives--
        subq.w  #1,g_Lives

        | Once the ship explodes, we don't check other enemies
        bra     .aces_break

.aces_next:
        adda.w  #TENEMY_SIZE,a2
        dbra    d4,.aces_loop

.aces_break:
        movem.l (sp)+,d2-d5/a2
.aces_done:
        rts

| ============================================================
| void AsmCollideEnemyShotsShip(void)
| ============================================================
        .global AsmCollideEnemyShotsShip
AsmCollideEnemyShotsShip:
        tst.w   g_ShipExploding
        bne     .acess_cleanup
        movem.l d2-d7/a2,-(sp)

        lea     g_EnemyShots,a2
        clr.w   d4                 | d4 = index i (0)
.acess_loop:
        tst.w   TES_ACTIVE(a2)
        beq     .acess_next

        | If ship is exploding, we break
        tst.w   g_ShipExploding
        bne     .acess_break

        move.w  TES_VARIANT(a2),d0
        cmp.w   g_ShipPolarity,d0
        bne     .acess_damage_check

        | --- Absorption check (same polarity) ---
        | dx = sx - g_ShipX - 4
        move.w  TES_X(a2),d0
        sub.w   g_ShipX,d0
        subq.w  #4,d0
        muls.w  d0,d0              | d0 = dx^2

        | dy = sy - g_ShipY - 10
        move.w  TES_Y(a2),d1
        sub.w   g_ShipY,d1
        subi.w  #10,d1
        muls.w  d1,d1              | d1 = dy^2

        add.l   d1,d0              | d0 = dx^2 + dy^2
        cmpi.l  #256,d0            | ABSORB_RADIUS_SQ = 256
        bgt     .acess_next        | if dist2 > 256, no absorption

        | Absorb! Call AbsorbEnemyShot(i)
        ext.l   d4
        move.l  d4,-(sp)
        jsr     AbsorbEnemyShot
        addq.l  #4,sp
        bra     .acess_next

.acess_damage_check:
        | --- Damage check (opposite polarity) ---
        | sx + ENEMYSHOT_W (10) > g_ShipX + SHIP_HIT_OX (3)
        move.w  g_ShipX,d0
        addq.w  #3,d0
        move.w  TES_X(a2),d1
        add.w   #10,d1
        cmp.w   d0,d1
        ble     .acess_next

        | sx < g_ShipX + SHIP_HIT_OX + SHIP_HIT_W (3 + 12 = 15)
        move.w  TES_X(a2),d1
        move.w  g_ShipX,d0
        add.w   #15,d0
        cmp.w   d0,d1
        bge     .acess_next

        | sy + ENEMYSHOT_H (4) > g_ShipY + SHIP_HIT_OY (6)
        move.w  g_ShipY,d0
        addq.w  #6,d0
        move.w  TES_Y(a2),d1
        addq.w  #4,d1
        cmp.w   d0,d1
        ble     .acess_next

        | sy < g_ShipY + SHIP_HIT_OY + SHIP_HIT_H (6 + 12 = 18)
        move.w  TES_Y(a2),d1
        move.w  g_ShipY,d0
        add.w   #18,d0
        cmp.w   d0,d1
        bge     .acess_next

        | COLLISION DETECTED (DAMAGE)!
        clr.w   TES_ACTIVE(a2)     | g_EnemyShots[i].active = 0

        | SpawnExplosion(g_ShipX, g_ShipY, EXP_KIND_SHIP = 1)
        move.w  #1,d0
        ext.l   d0
        move.l  d0,-(sp)
        move.w  g_ShipY,d0
        ext.l   d0
        move.l  d0,-(sp)
        move.w  g_ShipX,d0
        ext.l   d0
        move.l  d0,-(sp)
        jsr     SpawnExplosion
        lea     12(sp),sp

        | g_ShipExploding = 1
        move.w  #1,g_ShipExploding

        | g_ShipExplTimer = SHIP_EXPL_TIMER (12)
        move.w  #12,g_ShipExplTimer

        | g_Lives--
        subq.w  #1,g_Lives

.acess_next:
        addq.w  #1,d4              | i++
        adda.w  #TES_SIZE,a2       | next enemy shot
        cmpi.w  #MAX_ENEMY_SHOTS,d4
        blt.w   .acess_loop

.acess_break:
        movem.l (sp)+,d2-d7/a2
.acess_cleanup:
        | cleanup removed
.acess_done:
        rts
