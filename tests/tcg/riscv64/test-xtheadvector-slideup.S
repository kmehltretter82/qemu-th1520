/*
 * XTheadVector slide-up permutation boundary test.
 *
 * The companion permutation payload covers the slide-down and register-gather
 * forms.  This one covers the three slide-up forms, which have no dynamic
 * coverage: th.vslideup.vx, th.vslideup.vi and th.vslide1up.vx.
 *
 * Slide-up differs from slide-down in two ways that are checked here.  Its
 * destination elements below the offset are undisturbed rather than zeroed,
 * and frozen RVV 0.7.1 forbids an in-place destination because lower lanes
 * are written before the higher source lanes they would overwrite are read.
 * The slide-down forms deliberately allow that same in-place encoding.
 *
 * Vector instructions are emitted as words so the test does not require an
 * assembler with XTheadVector support.  An independent scalar RV64 loop
 * computes the expected values using overflow-safe index arithmetic.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ MSTATUS_VS_THEAD, (1 << 23)

    .macro SET_E8_M1
    .word   0x00037057      /* th.vsetvli zero, t1, e8, m1 */
    .endm

    .macro LOAD_VECTORS mask
    li      t1, 16
    SET_E8_M1
    lla     t2, source
    .word   0x12038087      /* th.vlb.v v1, (t2) */
    lla     t2, old_dest
    .word   0x12038107      /* th.vlb.v v2, (t2) */
    lla     t2, \mask
    .word   0x12038007      /* th.vlb.v v0, (t2) */
    .endm

    .macro STORE_V2
    li      t1, 16
    SET_E8_M1
    lla     t3, output
    .word   0x020e0127      /* th.vsb.v v2, (t3) */
    .endm

    .macro EXPECT_ILLEGAL encoding, count, failure
    li      s0, 1
    .word   \encoding
    li      s0, 0
    li      t0, \count
    beq     s1, t0, 1f
    li      a0, \failure
    j       exit
1:
    .endm

    .text
    .global _start
_start:
    csrr    t0, mhartid
    bnez    t0, secondary_hart
    lla     t0, trap
    csrw    mtvec, t0
    li      t0, MSTATUS_VS_THEAD
    csrs    mstatus, t0
    li      s0, 0             /* Any trap is unexpected until enabled below. */
    li      s1, 0             /* Count accepted illegal-instruction traps. */

    /* Offset 2: vd[0..1] keep the old destination, vd[2..7] slide up. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 2
    li      a5, 8
    li      a6, 0
    li      a7, 0
    li      t5, 0
    call    oracle_slideup
    LOAD_VECTORS mask_all
    li      t1, 8
    SET_E8_M1
    li      t0, 2
    .word   0x3a12c157      /* th.vslideup.vx v2, v1, t0 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 1
    call    check_16

    /* Offset 0 writes the whole active body and still clears the tail. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 0
    li      a5, 8
    li      a6, 0
    li      a7, 0
    li      t5, 0
    call    oracle_slideup
    LOAD_VECTORS mask_all
    li      t1, 8
    SET_E8_M1
    li      t0, 0
    .word   0x3a12c157      /* th.vslideup.vx v2, v1, t0 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 2
    call    check_16

    /* An offset at or above vl leaves the whole active body undisturbed. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 8
    li      a5, 8
    li      a6, 0
    li      a7, 0
    li      t5, 0
    call    oracle_slideup
    LOAD_VECTORS mask_all
    li      t1, 8
    SET_E8_M1
    li      t0, 8
    .word   0x3a12c157      /* th.vslideup.vx v2, v1, t0 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 3
    call    check_16

    /*
     * A full-XLEN offset must not wrap back into a valid source lane.  This
     * is the slide-up counterpart of the slide-down bound fixed in UQ-L013.
     */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, -1            /* 0xffffffffffffffff */
    li      a5, 8
    li      a6, 0
    li      a7, 0
    li      t5, 0
    call    oracle_slideup
    LOAD_VECTORS mask_all
    li      t1, 8
    SET_E8_M1
    li      t0, -1
    .word   0x3a12c157      /* th.vslideup.vx v2, v1, t0 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 4
    call    check_16

    /* Masking leaves inactive body elements undisturbed. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_alt
    li      a4, 2
    li      a5, 8
    li      a6, 0
    li      a7, 1
    li      t5, 0
    call    oracle_slideup
    LOAD_VECTORS mask_alt
    li      t1, 8
    SET_E8_M1
    li      t0, 2
    .word   0x3812c157      /* th.vslideup.vx v2, v1, t0, v0.t */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 5
    call    check_16

    /* A nonzero vstart above the offset skips the earlier body elements. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 2
    li      a5, 8
    li      a6, 5
    li      a7, 0
    li      t5, 0
    call    oracle_slideup
    LOAD_VECTORS mask_all
    li      t1, 8
    SET_E8_M1
    li      t0, 5
    csrw    0x008, t0       /* th.vstart */
    li      t0, 2
    .word   0x3a12c157      /* th.vslideup.vx v2, v1, t0 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 6
    call    check_16

    /* The immediate form takes the same path with a zero-extended offset. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 2
    li      a5, 8
    li      a6, 0
    li      a7, 0
    li      t5, 0
    call    oracle_slideup
    LOAD_VECTORS mask_all
    li      t1, 8
    SET_E8_M1
    .word   0x3a113157      /* th.vslideup.vi v2, v1, 2 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 7
    call    check_16

    /* th.vslide1up.vx places the scalar in element zero. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 1
    li      a5, 8
    li      a6, 0
    li      a7, 0
    li      t5, 0x5a
    call    oracle_slideup
    LOAD_VECTORS mask_all
    li      t1, 8
    SET_E8_M1
    li      t0, 0x5a
    .word   0x3a12e157      /* th.vslide1up.vx v2, v1, t0 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 8
    call    check_16

    /* With vstart 1 the scalar element is skipped but the shift continues. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 1
    li      a5, 8
    li      a6, 1
    li      a7, 0
    li      t5, 0x5a
    call    oracle_slideup
    LOAD_VECTORS mask_all
    li      t1, 8
    SET_E8_M1
    li      t0, 1
    csrw    0x008, t0       /* th.vstart */
    li      t0, 0x5a
    .word   0x3a12e157      /* th.vslide1up.vx v2, v1, t0 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 9
    call    check_16

    /*
     * Legality.  Frozen RVV 0.7.1 forbids a masked destination overlapping v0
     * for every slide form at any LMUL, and forbids an in-place slide-up
     * destination.  Unmasked vd=v0 and a non-overlapping masked destination
     * remain legal.
     */
    LOAD_VECTORS mask_alt
    li      t1, 8
    SET_E8_M1
    li      t0, 2

    EXPECT_ILLEGAL 0x3812c057, 1, 20    /* th.vslideup.vx v0, v1, t0, v0.t */
    EXPECT_ILLEGAL 0x38113057, 2, 21    /* th.vslideup.vi v0, v1, 2, v0.t */
    EXPECT_ILLEGAL 0x3812e057, 3, 22    /* th.vslide1up.vx v0, v1, t0, v0.t */
    EXPECT_ILLEGAL 0x3a12c0d7, 4, 23    /* th.vslideup.vx v1, v1, t0 */
    EXPECT_ILLEGAL 0x3a12e0d7, 5, 24    /* th.vslide1up.vx v1, v1, t0 */
    EXPECT_ILLEGAL 0x3a1130d7, 6, 25    /* th.vslideup.vi v1, v1, 2 */

    /* Legal controls: no further trap may be taken. */
    li      s0, 0
    .word   0x3a12c057      /* th.vslideup.vx v0, v1, t0 */
    .word   0x3812c157      /* th.vslideup.vx v2, v1, t0, v0.t */
    .word   0x3e12c0d7      /* th.vslidedown.vx v1, v1, t0 (in place is legal) */

    li      t0, 6
    beq     s1, t0, 1f
    li      a0, 26
    j       exit
1:

    li      a0, 0
    j       exit

/*
 * a0 expected, a1 old destination, a2 source, a3 mask, a4 offset,
 * a5 vl, a6 vstart, a7 masked, t5 slide1up scalar (a4 must be 1) or 0 for
 * the plain slide-up forms.  VLMAX is 16 for e8,m1.
 *
 * t6 selects the slide1up behaviour: it is set when the caller passes a
 * nonzero scalar, which the payload only does for th.vslide1up.vx.
 */
oracle_slideup:
    mv      t6, t5
    li      t0, 0
.Lup_copy:
    add     t1, a1, t0
    lbu     t2, 0(t1)
    add     t1, a0, t0
    sb      t2, 0(t1)
    addi    t0, t0, 1
    li      t1, 16
    bltu    t0, t1, .Lup_copy

    /*
     * The plain slide-up forms start at max(vstart, offset), so an offset at
     * or above vl writes nothing.  th.vslide1up.vx instead starts at vstart,
     * because it writes element zero from the scalar rather than from a
     * source lane.
     */
    mv      t0, a6
    bnez    t6, .Lup_body
    bgeu    t0, a4, .Lup_body
    mv      t0, a4
.Lup_body:
    bgeu    t0, a5, .Lup_tail
    beqz    a7, .Lup_active
    add     t1, a3, t0
    lbu     t2, 0(t1)
    andi    t2, t2, 1
    beqz    t2, .Lup_next
.Lup_active:
    bnez    t6, .Lup_slide1
    sub     t4, t0, a4
    add     t4, a2, t4
    lbu     t3, 0(t4)
    j       .Lup_store
.Lup_slide1:
    bnez    t0, .Lup_slide1_shift
    mv      t3, t6
    j       .Lup_store
.Lup_slide1_shift:
    addi    t4, t0, -1
    add     t4, a2, t4
    lbu     t3, 0(t4)
.Lup_store:
    add     t4, a0, t0
    sb      t3, 0(t4)
.Lup_next:
    addi    t0, t0, 1
    j       .Lup_body

.Lup_tail:
    mv      t0, a5
.Lup_tail_loop:
    li      t1, 16
    bgeu    t0, t1, .Lup_done
    add     t1, a0, t0
    sb      zero, 0(t1)
    addi    t0, t0, 1
    j       .Lup_tail_loop
.Lup_done:
    ret

check_16:
    li      t0, 16
.Lcheck_loop:
    lbu     t1, 0(a0)
    lbu     t2, 0(a1)
    bne     t1, t2, .Lcheck_failed
    addi    a0, a0, 1
    addi    a1, a1, 1
    addi    t0, t0, -1
    bnez    t0, .Lcheck_loop
    ret
.Lcheck_failed:
    mv      a0, a2
    j       exit

trap:
    csrr    t0, mcause
    li      t1, 2
    bne     t0, t1, .Lbad_trap_cause
    li      t0, 1
    bne     s0, t0, .Lunexpected_trap
    addi    s1, s1, 1
    csrr    t0, mepc
    addi    t0, t0, 4
    csrw    mepc, t0
    mret
.Lbad_trap_cause:
    li      a0, 90
    j       exit
.Lunexpected_trap:
    li      a0, 91
    j       exit

secondary_hart:
    wfi
    j       secondary_hart

exit:
    lla     a1, semiargs
    sd      a0, 8(a1)
    li      t0, 0x20026    /* ADP_Stopped_ApplicationExit */
    sd      t0, 0(a1)
    li      a0, 0x20       /* TARGET_SYS_EXIT_EXTENDED */

    .balign 16
    slli    zero, zero, 0x1f
    ebreak
    srai    zero, zero, 0x7
    j       .

    .section .rodata
    .balign 16
source:
    .byte   0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
    .byte   0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
old_dest:
    .byte   0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7
    .byte   0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf
mask_all:
    .byte   1, 1, 1, 1, 1, 1, 1, 1
    .byte   1, 1, 1, 1, 1, 1, 1, 1
mask_alt:
    .byte   1, 0, 1, 0, 1, 0, 1, 0
    .byte   1, 0, 1, 0, 1, 0, 1, 0

    .data
    .balign 16
expected:
    .space  128
output:
    .space  128
semiargs:
    .space  16
