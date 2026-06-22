| blitter.s - Amiga Blitter routines for nau_dx
| GAS m68k syntax: comments with |, labels end with :

        .equ    CUSTOM,   0xdff000
        .equ    DMACONR,  0x002
        .equ    BLTCON0,  0x040
        .equ    BLTCON1,  0x042
        .equ    BLTAFWM,  0x044
        .equ    BLTALWM,  0x046
        .equ    BLTDPTH,  0x054
        .equ    BLTDPTL,  0x056
        .equ    BLTSIZE,  0x058
        .equ    BLTADAT,  0x074
        .equ    BLTBDAT,  0x072
        .equ    BLTCDAT,  0x070
        .equ    DMACON,   0x096
        .equ    BLTDMOD,  0x066

        .text

        .global ClearGameAreaAsm
ClearGameAreaAsm:
| Clear full PF2 planes 1,3,5 (bytes 0-39 = 40 bytes = full width).
| No border — cloud bg fills whole screen.
| Stack: +4=return, +8=screen_mem
        movem.l d0-d7,-(sp)             | 8 regs = 32 bytes
        move.l  36(sp),a0               | a0 = screen_mem

        moveq   #0,d0
        move.l  d0,d1
        move.l  d0,d2
        move.l  d0,d3
        move.l  d0,d4
        move.l  d0,d5
        move.l  d0,d6
        move.l  d0,d7

        | --- Plane 1 (BPL2) = screen + 10240 ---
        lea     10240(a0),a1
        move.w  #255,d7
.cga_p1:
        movem.l d0-d6,(a1)              | 28 bytes (d0-d6)
        clr.l   28(a1)                  | bytes 28-31
        clr.l   32(a1)                  | bytes 32-35
        clr.l   36(a1)                  | bytes 36-39
        lea     40(a1),a1
        dbra    d7,.cga_p1

        | --- Plane 3 (BPL4) = screen + 30720 ---
        lea     30720(a0),a1
        move.w  #255,d7
.cga_p3:
        movem.l d0-d6,(a1)
        clr.l   28(a1)
        clr.l   32(a1)
        clr.l   36(a1)
        lea     40(a1),a1
        dbra    d7,.cga_p3

        | --- Plane 5 (BPL6) = screen + 51200 ---
        lea     30720(a0),a1
        lea     20480(a1),a1
        move.w  #255,d7
.cga_p5:
        movem.l d0-d6,(a1)
        clr.l   28(a1)
        clr.l   32(a1)
        clr.l   36(a1)
        lea     40(a1),a1
        dbra    d7,.cga_p5

        movem.l (sp)+,d0-d7
        rts



| ============================================================
| void ParallaxDrawAsm(void* screen_mem,
|                      const UWORD* tileSolid,
|                      const UWORD* tileDeco,
|                      const short* scrolls)
|
| Restores wall bytes on planes 1-4 and writes tile pattern on plane 0.
| Plane 1: bytes 0-3,32-35 = 0x00.  Planes 2-4: bytes 0-3,32-35 = 0xFF.
| Plane 0: 8 tile bytes per row from tileSolid/tileDeco arrays.
| Stack (after movem 48 bytes): 52=screen, 56=tileSolid, 60=tileDeco, 64=scrolls
| ============================================================
        .global ParallaxDrawAsm
ParallaxDrawAsm:
        movem.l d0-d7/a0-a5,-(sp)      | 12 regs = 48 bytes

        move.l  52(sp),a0               | a0 = screen_mem (plane 0 base)
        move.l  56(sp),a2               | a2 = tileSolid
        move.l  60(sp),a3               | a3 = tileDeco
        move.l  64(sp),a4               | a4 = scrolls

        | --- PLANE 1: bytes 0-3 = 0, bytes 32-35 = 0 (256 rows) ---
        move.l  a0,a1
        lea     10240(a1),a1            | a1 = plane 1
        move.w  #255,d0
.pd_p1:
        clr.l   (a1)                    | bytes 0-3 = 0
        clr.l   32(a1)                  | bytes 32-35 = 0
        lea     40(a1),a1
        dbra    d0,.pd_p1

        | --- PLANES 2,3,4: bytes 0-3 = FF, bytes 32-35 = FF ---
        | a1 is now at plane 2 base
        moveq   #2,d1                   | 3 planes
.pd_hp_plane:
        move.w  #255,d0
.pd_hp_row:
        move.l  #0xFFFFFFFF,(a1)        | bytes 0-3
        move.l  #0xFFFFFFFF,32(a1)      | bytes 32-35
        lea     40(a1),a1
        dbra    d0,.pd_hp_row
        dbra    d1,.pd_hp_plane

        | --- PLANE 0: tile pattern ---
        | Load biased scroll values: bias = 256 - scroll (so index = (row+bias) & 63)
        move.w  (a4),d4
        neg.w   d4
        addi.w  #256,d4                 | d4 = bias0
        move.w  2(a4),d5
        neg.w   d5
        addi.w  #256,d5                 | d5 = bias1
        move.w  4(a4),d6
        neg.w   d6
        addi.w  #256,d6                 | d6 = bias2
        move.w  6(a4),d7
        neg.w   d7
        addi.w  #256,d7                 | d7 = bias3

        move.l  a0,a1                   | a1 = plane 0 base
        move.w  #255,d3                 | 256 rows
        moveq   #0,d0                   | row = 0

.pd_p0_row:
        | t3 index -> tileSolid[t3]
        move.w  d0,d1
        add.w   d7,d1
        andi.w  #63,d1
        add.w   d1,d1                   | word offset
        move.w  (a2,d1.w),d2           | d2 = tileSolid[t3]
        | byte 0 = high byte, byte 35 = low byte
        move.b  d2,35(a1)
        lsr.w   #8,d2
        move.b  d2,(a1)

        | t2 index -> tileDeco[t2]
        move.w  d0,d1
        add.w   d6,d1
        andi.w  #63,d1
        add.w   d1,d1
        move.w  (a3,d1.w),d2           | d2 = tileDeco[t2]
        | byte 1 = low byte, byte 34 = high byte
        move.b  d2,1(a1)
        lsr.w   #8,d2
        move.b  d2,34(a1)

        | t1 index -> tileDeco[t1]
        move.w  d0,d1
        add.w   d5,d1
        andi.w  #63,d1
        add.w   d1,d1
        move.w  (a3,d1.w),d2           | d2 = tileDeco[t1]
        | byte 2 = high byte, byte 33 = low byte
        move.b  d2,33(a1)
        lsr.w   #8,d2
        move.b  d2,2(a1)

        | t0 index -> tileDeco[t0]
        move.w  d0,d1
        add.w   d4,d1
        andi.w  #63,d1
        add.w   d1,d1
        move.w  (a3,d1.w),d2           | d2 = tileDeco[t0]
        | byte 3 = low byte, byte 32 = high byte
        move.b  d2,3(a1)
        lsr.w   #8,d2
        move.b  d2,32(a1)

        lea     40(a1),a1              | next row
        addq.w  #1,d0
        dbra    d3,.pd_p0_row

        movem.l (sp)+,d0-d7/a0-a5
        rts

| ============================================================
| void ClearAndParallaxAsm(void* screen_mem,
|                          const UWORD* tileSolid,
|                          const UWORD* tileDeco,
|                          const short* scrolls)
|
| Combined clear + parallax wall draw, single pass per plane.
| Stack (after movem 56 bytes): 60=screen, 64=tileSolid, 68=tileDeco, 72=scrolls
| ============================================================
        .global ClearAndParallaxAsm
ClearAndParallaxAsm:
        movem.l d0-d7/a0-a6,-(sp)

        move.l  60(sp),a0               | a0 = screen_mem
        move.l  64(sp),a2               | a2 = tileSolid
        move.l  68(sp),a3               | a3 = tileDeco
        move.l  72(sp),a4               | a4 = scrolls ptr

        | Precompute biased scroll values (256 - scroll) for easy index calc
        move.w  (a4),d4
        neg.w   d4
        addi.w  #256,d4                 | d4 = bias0
        move.w  2(a4),d5
        neg.w   d5
        addi.w  #256,d5                 | d5 = bias1
        move.w  4(a4),d6
        neg.w   d6
        addi.w  #256,d6                 | d6 = bias2
        move.w  6(a4),d7
        neg.w   d7
        addi.w  #256,d7                 | d7 = bias3

| ---- PLANE 0: tile pattern + clear middle ----
        move.l  a0,a1                   | a1 = plane0 ptr (advances per row)
        move.w  #255,d3                 | 256 rows
        moveq   #0,d0                   | row counter
.cp0_row:
        | tile indices -> tile bytes
        | t3: byte0=tileSolid[t3]>>8, byte35=tileSolid[t3]&FF
        move.w  d0,d1
        add.w   d7,d1
        andi.w  #63,d1
        add.w   d1,d1
        move.w  (a2,d1.w),d2           | d2 = tileSolid[t3]
        move.b  d2,35(a1)             | byte35 = low byte
        lsr.w   #8,d2
        move.b  d2,(a1)               | byte0 = high byte

        | t2: byte1=tileDeco[t2]&FF, byte34=tileDeco[t2]>>8
        move.w  d0,d1
        add.w   d6,d1
        andi.w  #63,d1
        add.w   d1,d1
        move.w  (a3,d1.w),d2           | d2 = tileDeco[t2]
        move.b  d2,1(a1)              | byte1 = low byte
        lsr.w   #8,d2
        move.b  d2,34(a1)             | byte34 = high byte

        | t1: byte2=tileDeco[t1]>>8, byte33=tileDeco[t1]&FF
        move.w  d0,d1
        add.w   d5,d1
        andi.w  #63,d1
        add.w   d1,d1
        move.w  (a3,d1.w),d2           | d2 = tileDeco[t1]
        move.b  d2,33(a1)             | byte33 = low byte
        lsr.w   #8,d2
        move.b  d2,2(a1)              | byte2 = high byte

        | t0: byte3=tileDeco[t0]&FF, byte32=tileDeco[t0]>>8
        move.w  d0,d1
        add.w   d4,d1
        andi.w  #63,d1
        add.w   d1,d1
        move.w  (a3,d1.w),d2           | d2 = tileDeco[t0]
        move.b  d2,3(a1)              | byte3 = low byte
        lsr.w   #8,d2
        move.b  d2,32(a1)             | byte32 = high byte

        | Clear words 2..15 (bytes 4..31)
        clr.l   4(a1)
        clr.l   8(a1)
        clr.l   12(a1)
        clr.l   16(a1)
        clr.l   20(a1)
        clr.l   24(a1)
        clr.l   28(a1)
        | Clear bytes 36-39
        clr.l   36(a1)

        lea     40(a1),a1              | next row
        addq.w  #1,d0
        dbra    d3,.cp0_row

| ---- PLANE 1: all zeros ----
        | a1 is already at plane0 + 10240 = plane1 base
        move.w  #255,d3
.cp1_row:
        clr.l   (a1)
        clr.l   4(a1)
        clr.l   8(a1)
        clr.l   12(a1)
        clr.l   16(a1)
        clr.l   20(a1)
        clr.l   24(a1)
        clr.l   28(a1)
        clr.l   32(a1)
        clr.l   36(a1)
        lea     40(a1),a1
        dbra    d3,.cp1_row

| ---- PLANES 2, 3, 4: walls=FF, middle=0 ----
        | a1 is now at plane2 base
        moveq   #2,d3                   | 3 planes
.cp_hi_plane:
        move.w  #255,d2                 | 256 rows
.cp_hi_row:
        move.l  #0xFFFFFFFF,(a1)       | bytes 0-3 = FF
        clr.l   4(a1)
        clr.l   8(a1)
        clr.l   12(a1)
        clr.l   16(a1)
        clr.l   20(a1)
        clr.l   24(a1)
        clr.l   28(a1)
        move.l  #0xFFFFFFFF,32(a1)     | bytes 32-35 = FF
        clr.l   36(a1)
        lea     40(a1),a1
        dbra    d2,.cp_hi_row
        dbra    d3,.cp_hi_plane

        movem.l (sp)+,d0-d7/a0-a6
        rts

| ============================================================
| void DrawBorderAsm(UBYTE* screen_mem, const UBYTE* border_data,
|                    const UBYTE* border_mirror_data, short scroll_y)
|
| Draws 64px border on PF2 planes 1,3,5 (left + mirrored right).
| Hardcoded: BORDER_H=1024, row_bytes=8, plane_size=8192.
| Stack: sp+4=screen_mem, sp+8=border_data, sp+12=mirror, sp+16=scroll_y
| ============================================================
BORDER_H     = 1024
BORDER_RB    = 8
BORDER_PS    = 8192

        .global DrawBorderAsm
DrawBorderAsm:
        movem.l d0-d7/a2-a6,-(sp)      | 13 regs = 52 bytes

        move.l  56(sp),a3               | a3 = screen_mem
        move.l  60(sp),a4               | a4 = border_data
        move.l  64(sp),a5               | a5 = border_mirror_data
        move.l  68(sp),d7               | d7 = scroll_y

        | Ensure scroll in range (0..1023)
        andi.l  #1023,d7

        | Plane base pointers
        lea     10240(a3),a1            | a1 = plane 1 (BPL2)
        lea     30720(a3),a2            | a2 = plane 3 (3*10240, BPL4)
        lea     30720(a3),a6            | a6 = temp plane 3
        lea     20480(a6),a6            | a6 = plane 5 (51200 = 30720+20480, BPL6)

        | Row loop (256 iterations)
        move.w  #255,d6
.db_row:
        | d7 = scroll row, compute source offset = d7 * 8
        move.l  d7,d4
        lsl.l   #3,d4                   | d4 = d7 * 8

        | --- Plane 1 (BPL2): left border from border_data+0*8192 ---
        move.l  a4,a0
        adda.l  d4,a0
        movem.l (a0),d0-d1
        movem.l d0-d1,(a1)              | store to PF2 plane 1

        | --- Plane 3 (BPL4): left border from border_data+1*8192 ---
        move.l  a4,a0
        adda.l  #8192,a0
        adda.l  d4,a0
        movem.l (a0),d0-d1
        movem.l d0-d1,(a2)              | store to PF2 plane 3

        | --- Plane 5 (BPL6): left border from border_data+2*8192 ---
        move.l  a4,a0
        adda.l  #16384,a0
        adda.l  d4,a0
        movem.l (a0),d0-d1
        movem.l d0-d1,(a6)              | store to PF2 plane 5

        | --- Right border (mirrored): same for mirror data ---
        | Plane 1 (BPL2) right
        move.l  a5,a0
        adda.l  d4,a0
        movem.l (a0),d0-d1
        movem.l d0-d1,32(a1)

        | Plane 3 (BPL4) right
        move.l  a5,a0
        adda.l  #8192,a0
        adda.l  d4,a0
        movem.l (a0),d0-d1
        movem.l d0-d1,32(a2)

        | Plane 5 (BPL6) right
        move.l  a5,a0
        adda.l  #16384,a0
        adda.l  d4,a0
        movem.l (a0),d0-d1
        movem.l d0-d1,32(a6)

        | Next row: pointers +40
        lea     40(a1),a1
        lea     40(a2),a2
        lea     40(a6),a6

        | Wrap scroll
        addq.l  #1,d7
        cmpi.l  #BORDER_H,d7
        bcs.s   .db_nw
        moveq   #0,d7
.db_nw:
        dbra    d6,.db_row

.db_done:
        movem.l (sp)+,d0-d7/a2-a6
        rts


| ============================================================
| int DrawBob16Asm(UBYTE* screen_mem, const UWORD* mask, const UWORD* data,
|                  short x, short y, UBYTE colorMask, UWORD rows)
|
| Fast-path for word-aligned (x&1==0), fully visible 16px bobs on PF2.
| Returns 0 on success (drawn), 1 if needs C fallback.
| ============================================================
        .global DrawBob16Asm
DrawBob16Asm:
        movem.l d2-d7/a2-a6,-(sp)      | 11 regs = 44 bytes

        move.l  48(sp),a2               | a2 = screen_mem
        move.l  52(sp),a3               | a3 = mask ptr
        move.l  56(sp),a4               | a4 = data ptr
        move.w  62(sp),d7               | d7 = x (low 16)
        move.w  66(sp),d6               | d6 = y
        move.w  70(sp),d5               | d5 = colorMask
        move.w  74(sp),d4               | d4 = rows

        | --- Fast-path eligibility checks ---
        tst.w   d4
        beq     .db16_fail
        cmpi.w  #-16,d7
        ble     .db16_fail
        cmpi.w  #304,d7
        bgt     .db16_fail              | x > 304, right word would be off-screen
        btst    #0,d7
        bne     .db16_fail              | not word-aligned
        tst.w   d6
        bmi     .db16_fail              | y < 0, needs clipping

        | --- Setup ---
        | Plane bases: a0=plane1 (10240), a1=plane3 (30720), a5=plane5 (51200)
        lea     10240(a2),a0
        lea     30720(a2),a1
        lea     30720(a2),a5
        lea     20480(a5),a5

        | wx = x >> 4 (word index), byte offset = wx * 2
        move.w  d7,d2
        lsr.w   #4,d2                   | d2 = wx
        add.w   d2,d2                   | d2 = wx * 2 (byte offset)

        | row_base_word = y * 40 + wx * 2
        move.w  d6,d3
        lsl.w   #5,d3                   | d3 = y * 32
        move.w  d6,d0
        lsl.w   #3,d0                   | d0 = y * 8
        add.w   d0,d3                   | d3 = y * 40
        add.w   d2,d3                   | d3 = y*40 + wx*2 (byte offset)

        | Check row bounds
        move.w  d6,d0
        add.w   d4,d0                   | d0 = y + rows
        cmpi.w  #256,d0
        bgt     .db16_fail              | bottom clip needed

        | Per-row increment = 40 bytes per row
        moveq   #40,d2

        | --- Row loop ---
        subq.w  #1,d4                   | rows - 1 for dbra
.db16_rloop:
        | Load mask word
        move.w  (a3)+,d0                | d0 = mask[row]

        | Apply to plane 1 (BPL2)
        btst    #0,d5
        beq.s   .db16_p1c
        or.w    d0,(a0,d3.w)
        bra.s   .db16_p1d
.db16_p1c:
        move.w  d0,d1
        not.w   d1
        and.w   d1,(a0,d3.w)
.db16_p1d:

        | Apply to plane 3 (BPL4)
        btst    #1,d5
        beq.s   .db16_p3c
        or.w    d0,(a1,d3.w)
        bra.s   .db16_p3d
.db16_p3c:
        move.w  d0,d1
        not.w   d1
        and.w   d1,(a1,d3.w)
.db16_p3d:

        | Apply to plane 5 (BPL6)
        btst    #2,d5
        beq.s   .db16_p5c
        or.w    d0,(a5,d3.w)
        bra.s   .db16_p5d
.db16_p5c:
        move.w  d0,d1
        not.w   d1
        and.w   d1,(a5,d3.w)
.db16_p5d:

        | Next row
        adda.l  #2,a4                   | advance data pointer
        add.w   d2,d3                   | row offset += 40
        dbra    d4,.db16_rloop

        moveq   #0,d0                   | success
        bra.s   .db16_exit

.db16_fail:
        moveq   #1,d0                   | fallback needed
.db16_exit:
        movem.l (sp)+,d2-d7/a2-a6
        rts

| ============================================================
| int DrawBob32d2Asm(UBYTE* screen_mem, const UWORD* mask,
|                    const UWORD* dataHi, const UWORD* dataLo,
|                    short x, short y, UBYTE planeHi, UBYTE planeLo)
|
| Fast-path for word-aligned, fully visible 32x24 bobs with 2 data planes + clear plane5.
| Returns 0 on success, 1 if needs C fallback.
| Stack: sp+52=screen, +56=mask, +60=dataHi, +64=dataLo, +68=x, +72=y, +76=planeHi, +80=planeLo
| ============================================================
        .global DrawBob32d2Asm
DrawBob32d2Asm:
        movem.l d2-d7/a2-a6,-(sp)      | 11 regs = 44 bytes

        move.l  48(sp),a2               | a2 = screen_mem
        move.l  52(sp),a3               | a3 = mask
        move.l  56(sp),a4               | a4 = dataHi
        move.l  60(sp),a5               | a5 = dataLo
        move.w  66(sp),d7               | d7 = x (low 16 of int)
        move.w  70(sp),d6               | d6 = y
        move.w  74(sp),d5               | d5 = planeHi
        move.w  78(sp),d4               | d4 = planeLo

        | Fast-path checks
        btst    #0,d7
        bne     .db322_fail
        cmpi.w  #-32,d7
        ble     .db322_fail
        cmpi.w  #288,d7
        bgt     .db322_fail
        tst.w   d6
        bmi     .db322_fail

        | Setup plane pointers (plane index * 10240)
        move.w  d5,d0
        mulu    #10240,d0
        move.l  a2,a0
        adda.l  d0,a0                   | a0 = screen + planeHi*10240
        move.w  d4,d0
        mulu    #10240,d0
        move.l  a2,a1
        adda.l  d0,a1                   | a1 = screen + planeLo*10240
        | plane5 (always at 5*10240)
        lea     30720(a2),a6
        lea     20480(a6),a6            | a6 = plane5

        | wx = x >> 4, byte offset = wx * 2
        move.w  d7,d2
        lsr.w   #4,d2                   | d2 = wx
        add.w   d2,d2                   | d2 = wx * 2 (byte offset)

        | Row offset = y * 40 + wx * 2
        move.w  d6,d3
        lsl.w   #5,d3                   | y * 32
        move.w  d6,d0
        lsl.w   #3,d0                   | y * 8
        add.w   d0,d3                   | y * 40
        add.w   d2,d3                   | + wx*2

        | Check bottom: y + 24 > 256?
        addi.w  #24,d6
        cmpi.w  #256,d6
        bgt     .db322_fail

        | Row increment = 40 bytes
        moveq   #40,d2
        | 24 rows (use dbra)
        moveq   #23,d7

.db322_rloop:
        | Load mask words (2 per row for 32px)
        move.w  (a3)+,d0                | mask word 0
        move.w  (a3)+,d1                | mask word 1

        | Apply to planeHi (mask + dataHi)
        move.w  (a4)+,d5                | dataHi word 0
        move.w  (a4)+,d6                | dataHi word 1
        | Word 0: cookie-cut
        move.w  d0,d4
        not.w   d4
        and.w   d4,(a0,d3.w)            | clear mask area
        and.w   d0,d5                   | data & mask
        or.w    d5,(a0,d3.w)            | set data bits
        | Word 1 (offset +2 bytes)
        move.w  d1,d4
        not.w   d4
        and.w   d4,2(a0,d3.w)
        and.w   d1,d6
        or.w    d6,2(a0,d3.w)

        | Apply to planeLo (mask + dataLo)
        move.w  (a5)+,d5                | dataLo word 0
        move.w  (a5)+,d6                | dataLo word 1
        | Word 0
        move.w  d0,d4
        not.w   d4
        and.w   d4,(a1,d3.w)
        and.w   d0,d5
        or.w    d5,(a1,d3.w)
        | Word 1
        move.w  d1,d4
        not.w   d4
        and.w   d4,2(a1,d3.w)
        and.w   d1,d6
        or.w    d6,2(a1,d3.w)

        | Clear plane5 in mask area
        move.w  d0,d4
        not.w   d4
        and.w   d4,(a6,d3.w)
        move.w  d1,d4
        not.w   d4
        and.w   d4,2(a6,d3.w)

        | Next row
        add.w   d2,d3                   | row offset += 40
        dbra    d7,.db322_rloop

        moveq   #0,d0                   | success
        bra.s   .db322_exit

.db322_fail:
        moveq   #1,d0
.db322_exit:
        movem.l (sp)+,d2-d7/a2-a6
        rts

| ============================================================
| int DrawBob16d2Asm(UBYTE* screen_mem, const UWORD* mask,
|                    const UWORD* dataHi, const UWORD* dataLo,
|                    short x, short y, UBYTE planeHi, UBYTE planeLo, UWORD rows)
|
| Fast-path for word-aligned 16px bobs with 2 data planes + clear plane5.
| Returns 0 on success, 1 if needs C fallback.
| Stack: sp+52=screen, +56=mask, +60=dataHi, +64=dataLo, +68=x, +72=y,
|        +76=planeHi, +80=planeLo, +84=rows
| ============================================================
        .global DrawBob16d2Asm
DrawBob16d2Asm:
        movem.l d2-d7/a2-a6,-(sp)      | 11 regs = 44 bytes

        move.l  48(sp),a2               | a2 = screen_mem
        move.l  52(sp),a3               | a3 = mask
        move.l  56(sp),a4               | a4 = dataHi
        move.l  60(sp),a5               | a5 = dataLo
        move.w  66(sp),d7               | d7 = x
        move.w  70(sp),d6               | d6 = y
        move.w  74(sp),d5               | d5 = planeHi
        move.w  78(sp),d4               | d4 = planeLo
        move.w  82(sp),d3               | d3 = rows

        | Fast-path checks
        tst.w   d3
        beq     .db162_fail
        move.w  d7,d0
        andi.w  #15,d0
        bne     .db162_fail
        cmpi.w  #-16,d7
        ble     .db162_fail
        cmpi.w  #304,d7
        bgt     .db162_fail
        tst.w   d6
        bmi     .db162_fail

        | Plane pointers
        move.w  d5,d0
        mulu    #10240,d0
        move.l  a2,a0
        adda.l  d0,a0                   | a0 = planeHi
        move.w  d4,d0
        mulu    #10240,d0
        move.l  a2,a1
        adda.l  d0,a1                   | a1 = planeLo
        | Calculate pointer to the third PF2 plane (whichever of 1,3,5 is not planeHi/planeLo)
        | At this point: d5=planeHi, d4=planeLo, d7=x, d6=y
        | pl3rd = 9 - planeHi - planeLo
        moveq   #9,d0
        sub.w   d5,d0
        sub.w   d4,d0                   | d0 = pl3rd index (uses d5=planeHi, d4=planeLo)
        mulu    #10240,d0
        move.l  a2,a6
        adda.l  d0,a6                   | a6 = ptr to third PF2 plane

        | wx = x >> 4, byte offset = wx * 2
        move.w  d7,d2
        lsr.w   #4,d2
        add.w   d2,d2                   | d2 = wx * 2

        | Row offset = y * 40 + wx * 2
        move.w  d6,d1
        lsl.w   #5,d1                   | y * 32
        move.w  d6,d0
        lsl.w   #3,d0                   | y * 8
        add.w   d0,d1                   | y * 40
        add.w   d2,d1                   | + wx*2 -> d1 = row byte offset

        | Bottom check
        move.w  d6,d0
        add.w   d3,d0                   | y + rows
        cmpi.w  #256,d0
        bgt     .db162_fail

        | Save plane numbers into d7/d6 (x/y no longer needed)
        move.w  d5,d7                    | d7 = planeHi
        move.w  d4,d6                    | d6 = planeLo

        | dbra counter
        subq.w  #1,d3
        | Row increment
        moveq   #40,d2

.db162_rloop:
        move.w  (a3)+,d0                | mask

        | planeHi: cookie-cut
        move.w  (a4)+,d5
        move.w  d0,d4
        not.w   d4
        and.w   d4,(a0,d1.w)
        and.w   d0,d5
        or.w    d5,(a0,d1.w)

        | planeLo: cookie-cut
        move.w  (a5)+,d5
        move.w  d0,d4
        not.w   d4
        and.w   d4,(a1,d1.w)
        and.w   d0,d5
        or.w    d5,(a1,d1.w)

        | Always clear the third PF2 plane in the mask area
        move.w  d0,d4
        not.w   d4
        and.w   d4,(a6,d1.w)
.db162_clr_skip:

        | Next row
        add.w   d2,d1
        dbra    d3,.db162_rloop

        moveq   #0,d0
        bra.s   .db162_exit

.db162_fail:
        moveq   #1,d0
.db162_exit:
        movem.l (sp)+,d2-d7/a2-a6
        rts

| ============================================================
| int DrawForceFieldMaskAsm(void* screen_mem, short x, short y,
|                           const UWORD* mask, int planeIdx)
|
| Fast-path for fully-visible 32x32 masks ORed into one bitplane.
| Returns 0 on success, 1 if the C clipping fallback is needed.
| Stack after movem (40 bytes): +44=screen, +50=x, +54=y, +56=mask, +62=planeIdx
| ============================================================
        .global DrawForceFieldMaskAsm
DrawForceFieldMaskAsm:
        movem.l d2-d7/a2-a5,-(sp)

        move.l  44(sp),a2              | screen_mem
        move.w  50(sp),d7              | x
        move.w  54(sp),d6              | y
        move.l  56(sp),a3              | mask
        move.w  62(sp),d5              | planeIdx

        | Fully-visible fast path only.
        tst.w   d7
        bmi     .ffm_fail
        cmpi.w  #288,d7
        bgt     .ffm_fail
        tst.w   d6
        bmi     .ffm_fail
        cmpi.w  #224,d6
        bgt     .ffm_fail

        | a0 = screen_mem + planeIdx * 10240
        move.w  d5,d0
        mulu    #10240,d0
        move.l  a2,a0
        adda.l  d0,a0

        | d2 = y * 40 + (x >> 4) * 2
        move.w  d6,d2
        lsl.w   #5,d2                  | y * 32
        move.w  d6,d0
        lsl.w   #3,d0                  | y * 8
        add.w   d0,d2                  | y * 40
        move.w  d7,d0
        lsr.w   #4,d0
        add.w   d0,d0
        add.w   d0,d2

        move.w  d7,d4
        andi.w  #15,d4                 | shift
        moveq   #16,d5
        sub.w   d4,d5                  | invShift = 16 - shift
        move.w  #31,d3                 | 32 rows

        tst.w   d4
        beq.s   .ffm_zero_loop

.ffm_shift_loop:
        move.w  (a3)+,d0               | m0
        move.w  (a3)+,d1               | m1

        move.w  d0,d7                  | mv0 = m0 >> shift
        lsr.w   d4,d7
        or.w    d7,(a0,d2.w)

        lsl.w   d5,d0                  | mv1 = (m0 << invShift) | (m1 >> shift)
        move.w  d1,d7
        lsr.w   d4,d7
        or.w    d7,d0
        or.w    d0,2(a0,d2.w)

        move.w  d1,d6                  | mv2 = m1 << invShift
        lsl.w   d5,d6
        or.w    d6,4(a0,d2.w)

        addi.w  #40,d2
        dbra    d3,.ffm_shift_loop
        bra.s   .ffm_done

.ffm_zero_loop:
        move.w  (a3)+,d0
        move.w  (a3)+,d1
        or.w    d0,(a0,d2.w)
        or.w    d1,2(a0,d2.w)
        addi.w  #40,d2
        dbra    d3,.ffm_zero_loop

.ffm_done:
        moveq   #0,d0
        bra.s   .ffm_exit

.ffm_fail:
        moveq   #1,d0
.ffm_exit:
        movem.l (sp)+,d2-d7/a2-a5
        rts

| ============================================================
| int DrawForceFieldMask2Asm(void* screen_mem, short x, short y,
|                            const UWORD* mask, int planeA, int planeB)
|
| Fast-path for fully-visible 32x32 masks ORed into two bitplanes.
| Returns 0 on success, 1 if the C clipping fallback is needed.
| Stack after movem (48 bytes): +52=screen, +58=x, +62=y, +64=mask,
|                               +70=planeA, +74=planeB
| ============================================================
        .global DrawForceFieldMask2Asm
DrawForceFieldMask2Asm:
        movem.l d2-d7/a2-a7,-(sp)

        move.l  52(sp),a2              | screen_mem
        move.w  58(sp),d7              | x
        move.w  62(sp),d6              | y
        move.l  64(sp),a3              | mask
        move.w  70(sp),d5              | planeA
        move.w  74(sp),d4              | planeB

        | Fully-visible fast path only.
        tst.w   d7
        bmi     .ffm2_fail
        cmpi.w  #288,d7
        bgt     .ffm2_fail
        tst.w   d6
        bmi     .ffm2_fail
        cmpi.w  #224,d6
        bgt     .ffm2_fail

        | a0 = screen_mem + planeA * 10240
        move.w  d5,d0
        mulu    #10240,d0
        move.l  a2,a0
        adda.l  d0,a0

        | a1 = screen_mem + planeB * 10240
        move.w  d4,d0
        mulu    #10240,d0
        move.l  a2,a1
        adda.l  d0,a1

        | d2 = y * 40 + (x >> 4) * 2
        move.w  d6,d2
        lsl.w   #5,d2                  | y * 32
        move.w  d6,d0
        lsl.w   #3,d0                  | y * 8
        add.w   d0,d2                  | y * 40
        move.w  d7,d0
        lsr.w   #4,d0
        add.w   d0,d0
        add.w   d0,d2

        move.w  d7,d4
        andi.w  #15,d4                 | shift
        moveq   #16,d5
        sub.w   d4,d5                  | invShift = 16 - shift
        move.w  #31,d3                 | 32 rows

        tst.w   d4
        beq.s   .ffm2_zero_loop

.ffm2_shift_loop:
        move.w  (a3)+,d0               | m0
        move.w  (a3)+,d1               | m1

        move.w  d0,d7                  | mv0 = m0 >> shift
        lsr.w   d4,d7
        or.w    d7,(a0,d2.w)
        or.w    d7,(a1,d2.w)

        lsl.w   d5,d0                  | mv1 = (m0 << invShift) | (m1 >> shift)
        move.w  d1,d7
        lsr.w   d4,d7
        or.w    d7,d0
        or.w    d0,2(a0,d2.w)
        or.w    d0,2(a1,d2.w)

        move.w  d1,d6                  | mv2 = m1 << invShift
        lsl.w   d5,d6
        or.w    d6,4(a0,d2.w)
        or.w    d6,4(a1,d2.w)

        addi.w  #40,d2
        dbra    d3,.ffm2_shift_loop
        bra.s   .ffm2_done

.ffm2_zero_loop:
        move.w  (a3)+,d0
        move.w  (a3)+,d1
        or.w    d0,(a0,d2.w)
        or.w    d0,(a1,d2.w)
        or.w    d1,2(a0,d2.w)
        or.w    d1,2(a1,d2.w)
        addi.w  #40,d2
        dbra    d3,.ffm2_zero_loop

.ffm2_done:
        moveq   #0,d0
        bra.s   .ffm2_exit

.ffm2_fail:
        moveq   #1,d0
.ffm2_exit:
        movem.l (sp)+,d2-d7/a2-a7
        rts
