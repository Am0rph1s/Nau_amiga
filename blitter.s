| blitter.s - Amiga Blitter routines for nau_dx
| GAS m68k syntax: comments with |, labels end with :

        .equ    CUSTOM,   0xdff000
        .equ    DMACONR,  0x002
        .equ    BLTCON0,  0x040
        .equ    BLTCON1,  0x042
        .equ    BLTAFWM,  0x044
        .equ    BLTALWM,  0x046
        .equ    BLTDPTH,  0x054
        .equ    BLTSIZE,  0x058
        .equ    BLTDMOD,  0x06C

        .text

| ============================================================
| void WaitBlitter(void)
| Polls DMACONR bit 14 (BBUSY) until blitter is idle.
| Double-read workaround for A500/A1000 hardware bug.
| Clobbers d0.
| ============================================================
        .global WaitBlitter
WaitBlitter:
        move.w  DMACONR+CUSTOM,d0
        btst    #14,d0
        bne.s   WaitBlitter
        move.w  DMACONR+CUSTOM,d0
        btst    #14,d0
        bne.s   WaitBlitter
        rts

        | ============================================================
        | void BlitterClear(void* mem, ULONG bytes)
        | Clears `bytes` bytes to zero using the Blitter (D-channel).
        | Non-blocking: fires and returns immediately.
        | Call WaitBlitter() before accessing the cleared memory.
        |
        | Stack args (GCC m68k C calling convention):
        |   sp+4  = mem   (WORD-aligned pointer)
        |   sp+8  = bytes (must be multiple of 2)
        |
        | BLTSIZE: bits[15:6]=vsize (lines-1), bits[5:0]=hsize (words-1).
        | Max 64 words per line (hsize=6 bits), 1024 lines (vsize=10 bits).
        | bytes should be a multiple of 128 for exact fill.
        | Screen buffer = 320/8*256*5 = 51200 bytes = 25600 words
        | -> 400 lines of 64 words: BLTSIZE = (399<<6) | 63 = 25599
        | ============================================================
        .global BlitterClear
BlitterClear:
        movem.l d0-d2/a0-a1,-(sp)

        move.l  24(sp),a0               | a0 = mem  (5 regs*4 + ret addr = 24)
        move.l  28(sp),d0               | d0 = bytes

        lsr.l   #1,d0                   | d0 = total words
        beq     .bc_done                | nothing to clear

        lea     CUSTOM,a1

        | words_per_line = min(d0, 64)
        move.w  d0,d2
        cmpi.w  #64,d2
        bls.s   .bc_wpl_ok

        move.w  #64,d2                  | words_per_line = 64

        | lines = (total_words + 63) / 64
        move.l  d0,d1
        addi.l  #63,d1
        lsr.l   #6,d1                   | d1 = lines
        bra.s   .bc_build

.bc_wpl_ok:
        moveq   #1,d1                   | 1 line

.bc_build:
        | BLTSIZE = ((lines-1) << 6) | (words_per_line - 1)
        subq.w  #1,d2                   | hsize = wpl - 1
        subq.w  #1,d1                   | vsize = lines - 1
        lsl.w   #6,d1
        or.w    d2,d1                   | d1 = BLTSIZE

        | Wait for any previous blit (word read, not byte)
.bc_wait:
        move.w  DMACONR(a1),d0
        btst    #14,d0
        bne.s   .bc_wait
        move.w  DMACONR(a1),d0
        btst    #14,d0
        bne.s   .bc_wait

        | Setup blitter: D-only, minterm 0x00 = zero fill
        | BLTCON0 bit 8 (0x0100) = USED (D-channel enable); LF bits 7-0 = 0 -> output zeros
        move.w  #0x0100,BLTCON0(a1)     | USED=1, LF=0 -> writes zeros
        move.w  #0x0000,BLTCON1(a1)
        move.w  #0xffff,BLTAFWM(a1)
        move.w  #0xffff,BLTALWM(a1)
        move.w  #0,BLTDMOD(a1)          | contiguous
        | Write destination pointer as separate high/low words (OCS safe)
        move.l  a0,d0                   | copy addr to d0 for swap
        swap    d0                      | d0 = low:high
        move.w  d0,BLTDPTH(a1)          | write original HIGH word to BLTDPTH
        swap    d0                      | d0 = high:low (restored)
        move.w  d0,BLTDPTH+2(a1)        | write original LOW word to BLTDPTL (latch)
        move.w  d1,BLTSIZE(a1)          | fire! writing BLTSIZE starts the blit

.bc_done:
        movem.l (sp)+,d0-d2/a0-a1
        rts

| ============================================================
| void BlitterClearArea(void* addr, int w, int h, int dmod)
| Clears a rectangular area (w words x h lines) via Blitter D-channel.
| addr: word-aligned start address (byte pointer OK, but must be even)
| w: width in words (1-64)
| h: height in lines (1-1024)
| dmod: destination modulo in bytes (e.g. 12 for row-skip of 6 words)
| Non-blocking: fires and returns.
| Call WaitBlitter() before accessing the cleared memory.
| ============================================================
        .global BlitterClearArea
BlitterClearArea:
        movem.l d0-d2/a0-a1,-(sp)

        move.l  24(sp),a0               | a0 = addr
        move.l  28(sp),d0               | d0 = w (low word = value)
        move.l  32(sp),d1               | d1 = h
        move.l  36(sp),d2               | d2 = dmod

        | BLTSIZE = ((h-1) << 6) | (w-1)
        subq.w  #1,d0                   | w-1
        subq.w  #1,d1                   | h-1
        lsl.w   #6,d1
        or.w    d0,d1                   | d1 = BLTSIZE

        lea     CUSTOM,a1

.bca_wait:
        move.w  DMACONR(a1),d0
        btst    #14,d0
        bne.s   .bca_wait
        move.w  DMACONR(a1),d0
        btst    #14,d0
        bne.s   .bca_wait

        move.w  #0x0100,BLTCON0(a1)     | USED=1 (bit8), LF=0 -> zeros
        move.w  #0x0000,BLTCON1(a1)
        move.w  #0xffff,BLTAFWM(a1)
        move.w  #0xffff,BLTALWM(a1)
        move.w  d2,BLTDMOD(a1)          | modulo per line
        | Write destination pointer as separate high/low words (OCS safe)
        | BLTDPTH ($054) = high 16 bits, BLTDPTL ($056) = low 16 bits
        | swap only works on data registers (Dx), not address registers (Ax)
        move.l  a0,d0                   | copy addr to d0 for swap
        swap    d0                      | d0 = low:high
        move.w  d0,BLTDPTH(a1)          | write original HIGH word to BLTDPTH
        swap    d0                      | d0 = high:low (restored)
        move.w  d0,BLTDPTH+2(a1)        | write original LOW word to BLTDPTL (latch)
        move.w  d1,BLTSIZE(a1)          | fire!

        movem.l (sp)+,d0-d2/a0-a1
        rts

        .global ClearGameAreaAsm
ClearGameAreaAsm:
| Clear planes 1 and 3 only (PF2 in dual-playfield mode 4+2)
| Full 40 bytes per row (no wall preservation needed, walls are in PF1)
        movem.l d0-d1/a0-a1,-(sp)
        move.l  20(sp),a0
        | Plane 1 = a0 + 10240 (PF2 bit0)
        lea     10240(a0),a1
        move.w  #255,d0
.cga_p1_row:
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
        dbra    d0,.cga_p1_row
        | Plane 3 = advance from plane2 end + 10240 -> plane3 (PF2 bit1)
        lea     10240(a1),a1
        move.w  #255,d0
.cga_p3_row:
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
        dbra    d0,.cga_p3_row
        movem.l (sp)+,d0-d1/a0-a1
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
