/*
 * XTheadVector register-gather-immediate and compress boundary test.
 *
 * These are the last two XTheadVector permutation forms without dynamic
 * coverage: th.vrgather.vi and th.vcompress.vm.
 *
 * th.vcompress.vm is the only permutation form whose destination length is
 * decided by its mask rather than by vl: it packs the enabled elements of vs2
 * into the low elements of vd and clears from that count, not from vl.  Frozen
 * RVV 0.7.1 also requires a zero vstart and forbids a destination overlapping
 * either source, because the compaction reads lanes it has already written.
 *
 * Vector instructions are emitted as words so the test does not require an
 * assembler with XTheadVector support.  Independent scalar RV64 loops compute
 * the expected values.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ MSTATUS_VS_THEAD, (1 << 23)

    .macro SET_E8_M1
    .word   0x00037057      /* th.vsetvli zero, t1, e8, m1 */
    .endm

    .macro LOAD_VECTORS mask, vs1mask
    li      t1, 16
    SET_E8_M1
    lla     t2, source
    .word   0x12038087      /* th.vlb.v v1, (t2) */
    lla     t2, old_dest
    .word   0x12038107      /* th.vlb.v v2, (t2) */
    lla     t2, \mask
    .word   0x12038007      /* th.vlb.v v0, (t2) */
    lla     t2, \vs1mask
    .word   0x12038187      /* th.vlb.v v3, (t2) */
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

    /* th.vrgather.vi broadcasts one source lane across the active body. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 3
    li      a5, 8
    li      a6, 0
    li      a7, 0
    call    oracle_gather_imm
    LOAD_VECTORS mask_all, mask_alt
    li      t1, 8
    SET_E8_M1
    .word   0x3211b157      /* th.vrgather.vi v2, v1, 3 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 1
    call    check_16

    /* An immediate at or above VLMAX selects zero, not a wrapped lane. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 20
    li      a5, 8
    li      a6, 0
    li      a7, 0
    call    oracle_gather_imm
    LOAD_VECTORS mask_all, mask_alt
    li      t1, 8
    SET_E8_M1
    .word   0x321a3157      /* th.vrgather.vi v2, v1, 20 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 2
    call    check_16

    /* Masked gather leaves inactive body elements undisturbed. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_alt
    li      a4, 3
    li      a5, 8
    li      a6, 0
    li      a7, 1
    call    oracle_gather_imm
    LOAD_VECTORS mask_alt, mask_alt
    li      t1, 8
    SET_E8_M1
    .word   0x3011b157      /* th.vrgather.vi v2, v1, 3, v0.t */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 3
    call    check_16

    /* A nonzero vstart skips the earlier body elements. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 3
    li      a5, 8
    li      a6, 5
    li      a7, 0
    call    oracle_gather_imm
    LOAD_VECTORS mask_all, mask_alt
    li      t1, 8
    SET_E8_M1
    li      t0, 5
    csrw    0x008, t0       /* th.vstart */
    .word   0x3211b157      /* th.vrgather.vi v2, v1, 3 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 4
    call    check_16

    /* Compress packs the enabled lanes down and clears from that count. */
    lla     a0, expected
    lla     a1, source
    lla     a2, mask_alt
    li      a3, 8
    call    oracle_compress
    LOAD_VECTORS mask_all, mask_alt
    li      t1, 8
    SET_E8_M1
    .word   0x5e11a157      /* th.vcompress.vm v2, v1, v3 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 5
    call    check_16

    /* An all-ones mask compresses to a plain copy of the active body. */
    lla     a0, expected
    lla     a1, source
    lla     a2, mask_all
    li      a3, 8
    call    oracle_compress
    LOAD_VECTORS mask_all, mask_all
    li      t1, 8
    SET_E8_M1
    .word   0x5e11a157      /* th.vcompress.vm v2, v1, v3 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 6
    call    check_16

    /* An all-zero mask writes no element and clears the whole register. */
    lla     a0, expected
    lla     a1, source
    lla     a2, mask_none
    li      a3, 8
    call    oracle_compress
    LOAD_VECTORS mask_all, mask_none
    li      t1, 8
    SET_E8_M1
    .word   0x5e11a157      /* th.vcompress.vm v2, v1, v3 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 7
    call    check_16

    /*
     * With vl equal to VLMAX and no mask the translator takes an optimised
     * broadcast path instead of the helper, for both a valid index and an
     * out-of-range one.  The vl=8 cases above only reach the helper.
     */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 3
    li      a5, 16
    li      a6, 0
    li      a7, 0
    call    oracle_gather_imm
    LOAD_VECTORS mask_all, mask_alt
    li      t1, 16
    SET_E8_M1
    .word   0x3211b157      /* th.vrgather.vi v2, v1, 3 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 8
    call    check_16

    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 20
    li      a5, 16
    li      a6, 0
    li      a7, 0
    call    oracle_gather_imm
    LOAD_VECTORS mask_all, mask_alt
    li      t1, 16
    SET_E8_M1
    .word   0x321a3157      /* th.vrgather.vi v2, v1, 20 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 9
    call    check_16

    /*
     * An immediate of exactly VLMAX is the boundary between the two optimised
     * paths and must still select zero.  The out-of-range case above is too
     * far out to separate a >= test from a > test.
     */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 16
    li      a5, 16
    li      a6, 0
    li      a7, 0
    call    oracle_gather_imm
    LOAD_VECTORS mask_all, mask_alt
    li      t1, 16
    SET_E8_M1
    .word   0x32183157      /* th.vrgather.vi v2, v1, 16 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 10
    call    check_16

    /*
     * e64,m1 has VLMAX 2.  Compress at the widest element width checks that
     * the compacted store and the clear origin both use the element type
     * rather than a byte count.  The mask register keeps its byte layout: at
     * e64 the mask bit for element i lives in byte i*8.
     */
    lla     a0, expected
    lla     a1, source64
    lla     a2, mask64_lo
    call    oracle_compress64
    LOAD_VECTORS mask_all, mask64_lo
    li      t1, 2
    .word   0x00c37057      /* th.vsetvli zero, t1, e64, m1 */
    lla     t2, source64
    .word   0x0203f087      /* th.vle.v v1, (t2) */
    .word   0x5e11a157      /* th.vcompress.vm v2, v1, v3 */
    lla     t2, output
    .word   0x0203f127      /* th.vse.v v2, (t2) */
    lla     a0, output
    lla     a1, expected
    li      a2, 11
    call    check_16

    lla     a0, expected
    lla     a1, source64
    lla     a2, mask64_hi
    call    oracle_compress64
    LOAD_VECTORS mask_all, mask64_hi
    li      t1, 2
    .word   0x00c37057      /* th.vsetvli zero, t1, e64, m1 */
    lla     t2, source64
    .word   0x0203f087      /* th.vle.v v1, (t2) */
    .word   0x5e11a157      /* th.vcompress.vm v2, v1, v3 */
    lla     t2, output
    .word   0x0203f127      /* th.vse.v v2, (t2) */
    lla     a0, output
    lla     a1, expected
    li      a2, 12
    call    check_16

    lla     a0, expected
    lla     a1, source64
    lla     a2, mask64_both
    call    oracle_compress64
    LOAD_VECTORS mask_all, mask64_both
    li      t1, 2
    .word   0x00c37057      /* th.vsetvli zero, t1, e64, m1 */
    lla     t2, source64
    .word   0x0203f087      /* th.vle.v v1, (t2) */
    .word   0x5e11a157      /* th.vcompress.vm v2, v1, v3 */
    lla     t2, output
    .word   0x0203f127      /* th.vse.v v2, (t2) */
    lla     a0, output
    lla     a1, expected
    li      a2, 13
    call    check_16

    /* Restore e8,m1 for the legality block below. */
    li      t1, 16
    SET_E8_M1

    /*
     * Legality.  Compress requires a zero vstart and a destination that
     * overlaps neither source; the gather immediate keeps the shared masked
     * vd=v0 and non-overlapping destination rules.
     */
    LOAD_VECTORS mask_alt, mask_alt
    li      t1, 8
    SET_E8_M1

    EXPECT_ILLEGAL 0x3011b057, 1, 20    /* th.vrgather.vi v0, v1, 3, v0.t */
    EXPECT_ILLEGAL 0x3211b0d7, 2, 21    /* th.vrgather.vi v1, v1, 3 */
    EXPECT_ILLEGAL 0x5e11a0d7, 3, 22    /* th.vcompress.vm v1, v1, v3 */
    EXPECT_ILLEGAL 0x5e11a1d7, 4, 23    /* th.vcompress.vm v3, v1, v3 */

    /* A nonzero vstart makes an otherwise legal compress illegal. */
    li      t0, 1
    csrw    0x008, t0       /* th.vstart */
    EXPECT_ILLEGAL 0x5e11a157, 5, 24    /* th.vcompress.vm v2, v1, v3 */
    csrw    0x008, zero

    /* Legal controls: no further trap may be taken. */
    li      s0, 0
    .word   0x3211b157      /* th.vrgather.vi v2, v1, 3 */
    .word   0x3011b157      /* th.vrgather.vi v2, v1, 3, v0.t */
    .word   0x3211b057      /* th.vrgather.vi v0, v1, 3 (unmasked) */
    .word   0x5e11a157      /* th.vcompress.vm v2, v1, v3 */

    li      t0, 5
    beq     s1, t0, 1f
    li      a0, 25
    j       exit
1:

    li      a0, 0
    j       exit

/*
 * a0 expected, a1 old destination, a2 source, a3 mask, a4 index,
 * a5 vl, a6 vstart, a7 masked.  VLMAX is 16 for e8,m1.
 */
oracle_gather_imm:
    li      t0, 0
.Lgi_copy:
    add     t1, a1, t0
    lbu     t2, 0(t1)
    add     t1, a0, t0
    sb      t2, 0(t1)
    addi    t0, t0, 1
    li      t1, 16
    bltu    t0, t1, .Lgi_copy

    mv      t0, a6
.Lgi_body:
    bgeu    t0, a5, .Lgi_tail
    beqz    a7, .Lgi_active
    add     t1, a3, t0
    lbu     t2, 0(t1)
    andi    t2, t2, 1
    beqz    t2, .Lgi_next
.Lgi_active:
    li      t1, 16
    bgeu    a4, t1, .Lgi_zero
    add     t1, a2, a4
    lbu     t3, 0(t1)
    j       .Lgi_store
.Lgi_zero:
    li      t3, 0
.Lgi_store:
    add     t1, a0, t0
    sb      t3, 0(t1)
.Lgi_next:
    addi    t0, t0, 1
    j       .Lgi_body

.Lgi_tail:
    mv      t0, a5
.Lgi_tail_loop:
    li      t1, 16
    bgeu    t0, t1, .Lgi_done
    add     t1, a0, t0
    sb      zero, 0(t1)
    addi    t0, t0, 1
    j       .Lgi_tail_loop
.Lgi_done:
    ret

/*
 * a0 expected, a1 source, a2 mask, a3 vl.  Compress ignores the old
 * destination entirely: every element from the compacted count upwards is
 * cleared, so nothing of the previous contents survives.
 */
oracle_compress:
    li      t0, 0             /* source index */
    li      t4, 0             /* destination count */
.Lco_body:
    bgeu    t0, a3, .Lco_tail
    add     t1, a2, t0
    lbu     t2, 0(t1)
    andi    t2, t2, 1
    beqz    t2, .Lco_next
    add     t1, a1, t0
    lbu     t3, 0(t1)
    add     t1, a0, t4
    sb      t3, 0(t1)
    addi    t4, t4, 1
.Lco_next:
    addi    t0, t0, 1
    j       .Lco_body

.Lco_tail:
    mv      t0, t4
.Lco_tail_loop:
    li      t1, 16
    bgeu    t0, t1, .Lco_done
    add     t1, a0, t0
    sb      zero, 0(t1)
    addi    t0, t0, 1
    j       .Lco_tail_loop
.Lco_done:
    ret

/*
 * a0 expected, a1 source (2 dwords), a2 mask (byte i*8 carries element i).
 * VLMAX is 2 for e64,m1, so the clear runs from the compacted count to 2.
 */
oracle_compress64:
    li      t0, 0             /* source element index */
    li      t4, 0             /* destination count */
.Lco64_body:
    li      t1, 2
    bgeu    t0, t1, .Lco64_tail
    slli    t1, t0, 3
    add     t1, a2, t1
    lbu     t2, 0(t1)
    andi    t2, t2, 1
    beqz    t2, .Lco64_next
    slli    t1, t0, 3
    add     t1, a1, t1
    ld      t3, 0(t1)
    slli    t1, t4, 3
    add     t1, a0, t1
    sd      t3, 0(t1)
    addi    t4, t4, 1
.Lco64_next:
    addi    t0, t0, 1
    j       .Lco64_body

.Lco64_tail:
    mv      t0, t4
.Lco64_tail_loop:
    li      t1, 2
    bgeu    t0, t1, .Lco64_done
    slli    t1, t0, 3
    add     t1, a0, t1
    sd      zero, 0(t1)
    addi    t0, t0, 1
    j       .Lco64_tail_loop
.Lco64_done:
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
mask_none:
    .byte   0, 0, 0, 0, 0, 0, 0, 0
    .byte   0, 0, 0, 0, 0, 0, 0, 0
    .balign 16
source64:
    .dword  0x1122334455667788, 0x99aabbccddeeff00
mask64_lo:
    .byte   1, 0, 0, 0, 0, 0, 0, 0
    .byte   0, 0, 0, 0, 0, 0, 0, 0
mask64_hi:
    .byte   0, 0, 0, 0, 0, 0, 0, 0
    .byte   1, 0, 0, 0, 0, 0, 0, 0
mask64_both:
    .byte   1, 0, 0, 0, 0, 0, 0, 0
    .byte   1, 0, 0, 0, 0, 0, 0, 0

    .data
    .balign 16
expected:
    .space  128
output:
    .space  128
semiargs:
    .space  16
