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
        .equ    BLTDMOD,  0x066

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
        | BLTCON0 bit 8 = USED (D-channel enable); LF bits 7-0 = 0 -> output zeros
        move.w  #0x0100,BLTCON0(a1)     | USED=1, LF=0 -> writes zeros
        move.w  #0x0000,BLTCON1(a1)
        move.w  #0xffff,BLTAFWM(a1)
        move.w  #0xffff,BLTALWM(a1)
        move.w  #0,BLTDMOD(a1)          | contiguous
        move.l  a0,BLTDPTH(a1)          | destination
        move.w  d1,BLTSIZE(a1)          | fire! writing BLTSIZE starts the blit

.bc_done:
        movem.l (sp)+,d0-d2/a0-a1
        rts
