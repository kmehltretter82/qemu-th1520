/*
 * XTheadVector reduction boundary test.
 *
 * The vector instructions are emitted as words so this test does not require
 * an assembler with XTheadVector support.  Encodings come from the frozen
 * XTheadVector specification and target/riscv/xtheadvector.decode.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ MSTATUS_FS_INITIAL,  (1 << 13)
    .equ MSTATUS_VS_THEAD,    (1 << 23)

    .macro EXPECT_ILLEGAL encoding, count
    .word   \encoding
    li      t0, \count
    bne     s1, t0, fail_illegal
    .endm

    .text
    .global _start
_start:
    csrr    t0, mhartid
    bnez    t0, secondary_hart
    lla     t0, trap
    csrw    mtvec, t0
    li      s0, 0             /* Expected-trap selector. */
    li      s1, 0             /* Count of accepted illegal traps. */
    li      t0, MSTATUS_FS_INITIAL | MSTATUS_VS_THEAD
    csrs    mstatus, t0

    /* With vl=0, an integer reduction must not touch any destination byte. */
    li      t1, 16
    .word   0x00037057        /* th.vsetvli zero, t1, e8, m1 */
    lla     t2, old_dest
    .word   0x12038187        /* th.vlb.v v3, (t2) */
    li      t1, 0
    .word   0x00037057
    .word   0x021121d7        /* th.vredsum.vs v3, v1, v2 */
    li      t1, 16
    .word   0x00037057
    lla     t3, int_vl0_output
    .word   0x020e01a7        /* th.vsb.v v3, (t3) */
    lla     a0, int_vl0_output
    lla     a1, old_dest
    li      a2, 1
    call    check_16

    /* Floating-point reductions obey the same whole-register vl=0 rule. */
    lla     t2, old_dest
    .word   0x12038187        /* th.vlb.v v3, (t2) */
    li      t1, 0
    .word   0x00837057        /* th.vsetvli zero, t1, e32, m1 */
    .word   0x064091d7        /* th.vfredsum.vs v3, v4, v1 */
    li      t1, 16
    .word   0x00037057
    lla     t3, fp_vl0_output
    .word   0x020e01a7        /* th.vsb.v v3, (t3) */
    lla     a0, fp_vl0_output
    lla     a1, old_dest
    li      a2, 2
    call    check_16

    /* Every reduction is illegal when th.vstart is nonzero. */
    li      s0, 1
    li      t1, 4
    .word   0x00037057        /* th.vsetvli zero, t1, e8, m1 */
    li      t0, 1
    csrw    0x008, t0         /* th.vstart */
    EXPECT_ILLEGAL 0x021121d7, 1
    csrw    0x008, zero

    li      t1, 1
    .word   0x00837057        /* th.vsetvli zero, t1, e32, m1 */
    li      t0, 1
    csrw    0x008, t0
    EXPECT_ILLEGAL 0x064091d7, 2
    csrw    0x008, zero

    /*
     * There is no 64-to-128-bit widening helper.  Exercise each widening
     * dispatch class: narrow-vector sources, wide-vector sources, and a
     * scalar source.  The standard RVV decoder probes these encodings before
     * the XTheadVector decoder, so this also guards both translators' bounds
     * checks when the C910 has its ratified V extension disabled.
     */
    li      t1, 1
    .word   0x00c37057        /* th.vsetvli zero, t1, e64, m1 */
    EXPECT_ILLEGAL 0xc21101d7, 3
    EXPECT_ILLEGAL 0xc220a1d7, 4 /* th.vwaddu.vv v3, v1, v2 */
    EXPECT_ILLEGAL 0xd220a1d7, 5 /* th.vwaddu.wv v3, v1, v2 */
    EXPECT_ILLEGAL 0xd220e1d7, 6 /* th.vwaddu.wx v3, v1, t2 */

    /* The vector source group, unlike the scalar operands, follows LMUL. */
    li      t1, 1
    .word   0x00337057        /* th.vsetvli zero, t1, e8, m8 */
    EXPECT_ILLEGAL 0x021121d7, 7
    li      s0, 0

    /* Widening integer reductions accept LMUL=8 and any scalar registers. */
    li      t1, 16
    .word   0x00037057        /* th.vsetvli zero, t1, e8, m1 */
    lla     t2, int_accumulator
    .word   0x12038087        /* th.vlb.v v1, (t2) */
    lla     t2, old_dest
    .word   0x12038187        /* th.vlb.v v3, (t2) */
    li      t1, 4
    .word   0x00b37057        /* th.vsetvli zero, t1, e32, m8 */
    lla     t2, int_sources
    .word   0x0203f407        /* th.vle.v v8, (t2) */
    .word   0xc28081d7        /* th.vwredsumu.vs v3, v8, v1 */
    li      t1, 16
    .word   0x00037057
    lla     t3, int_lmul8_output
    .word   0x020e01a7        /* th.vsb.v v3, (t3) */
    lla     a0, int_lmul8_output
    lla     a1, int_lmul8_expected
    li      a2, 7
    call    check_16

    /* Floating-point widening reductions also accept an LMUL=8 source. */
    lla     t2, fp_accumulator
    .word   0x12038087        /* th.vlb.v v1, (t2) */
    lla     t2, old_dest
    .word   0x12038187        /* th.vlb.v v3, (t2) */
    li      t1, 4
    .word   0x00b37057        /* th.vsetvli zero, t1, e32, m8 */
    lla     t2, fp_sources
    .word   0x0203f407        /* th.vle.v v8, (t2) */
    .word   0xc68091d7        /* th.vfwredsum.vs v3, v8, v1 */
    li      t1, 16
    .word   0x00037057
    lla     t3, fp_lmul8_output
    .word   0x020e01a7        /* th.vsb.v v3, (t3) */
    lla     a0, fp_lmul8_output
    lla     a1, fp_lmul8_expected
    li      a2, 8
    call    check_16

    /* A scalar destination may overlap the mask used by the reduction. */
    lla     t2, mask_input
    .word   0x12038007        /* th.vlb.v v0, (t2) */
    lla     t2, byte_sources
    .word   0x12038087        /* th.vlb.v v1, (t2) */
    lla     t2, byte_accumulator
    .word   0x12038107        /* th.vlb.v v2, (t2) */
    li      t1, 4
    .word   0x00037057
    .word   0x00112057        /* th.vredsum.vs v0, v1, v2, v0.t */
    li      t1, 16
    .word   0x00037057
    lla     t3, mask_overlap_output
    .word   0x020e0027        /* th.vsb.v v0, (t3) */
    lla     a0, mask_overlap_output
    lla     a1, mask_overlap_expected
    li      a2, 9
    call    check_16

    /* The scalar destination may also overlap the vector source group. */
    lla     t2, byte_sources
    .word   0x12038087        /* th.vlb.v v1, (t2) */
    lla     t2, byte_accumulator
    .word   0x12038107        /* th.vlb.v v2, (t2) */
    li      t1, 4
    .word   0x00037057
    .word   0x021120d7        /* th.vredsum.vs v1, v1, v2 */
    li      t1, 16
    .word   0x00037057
    lla     t3, source_overlap_output
    .word   0x020e00a7        /* th.vsb.v v1, (t3) */
    lla     a0, source_overlap_output
    lla     a1, source_overlap_expected
    li      a2, 10
    call    check_16

    /* With no active elements, only the scalar input is copied. */
    lla     t2, zero_mask
    .word   0x12038007        /* th.vlb.v v0, (t2) */
    lla     t2, byte_sources
    .word   0x12038087        /* th.vlb.v v1, (t2) */
    lla     t2, byte_accumulator
    .word   0x12038107        /* th.vlb.v v2, (t2) */
    lla     t2, old_dest
    .word   0x12038187        /* th.vlb.v v3, (t2) */
    li      t1, 4
    .word   0x00037057
    .word   0x001121d7        /* th.vredsum.vs v3, v1, v2, v0.t */
    li      t1, 16
    .word   0x00037057
    lla     t3, inactive_output
    .word   0x020e01a7        /* th.vsb.v v3, (t3) */
    lla     a0, inactive_output
    lla     a1, inactive_expected
    li      a2, 11
    call    check_16

    li      a0, 0
    j       exit

check_16:
    li      t2, 16
1:
    lbu     t0, 0(a0)
    lbu     t1, 0(a1)
    bne     t0, t1, check_16_failed
    addi    a0, a0, 1
    addi    a1, a1, 1
    addi    t2, t2, -1
    bnez    t2, 1b
    ret
check_16_failed:
    mv      a0, a2
    j       exit

trap:
    csrr    t0, mcause
    li      t1, 2
    bne     t0, t1, fail_bad_cause
    li      t0, 1
    bne     s0, t0, fail_unexpected_trap
    addi    s1, s1, 1
    csrr    t0, mepc
    addi    t0, t0, 4
    csrw    mepc, t0
    mret

fail_illegal:
    addi    a0, t0, 20      /* 21..27 identify the missing trap. */
    j       exit
fail_bad_cause:       li a0, 90; j exit
fail_unexpected_trap: li a0, 91; j exit

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
old_dest:
    .byte   0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7
    .byte   0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf
int_accumulator:
    .dword  10, 0x7777777777777777
int_sources:
    .word   1, 2, 3, 4
int_lmul8_expected:
    .dword  20, 0
fp_accumulator:
    .dword  0x4024000000000000, 0x7777777777777777 /* 10.0 */
fp_sources:
    .word   0x3f800000, 0x40000000, 0x40400000, 0x40800000
fp_lmul8_expected:
    .dword  0x4034000000000000, 0 /* 20.0 */
mask_input:
    .byte   1, 0, 1, 0, 0, 0, 0, 0
    .byte   0, 0, 0, 0, 0, 0, 0, 0
zero_mask:
    .zero   16
byte_sources:
    .byte   1, 2, 3, 4, 0, 0, 0, 0
    .byte   0, 0, 0, 0, 0, 0, 0, 0
byte_accumulator:
    .byte   10, 0, 0, 0, 0, 0, 0, 0
    .byte   0, 0, 0, 0, 0, 0, 0, 0
mask_overlap_expected:
    .byte   14, 0, 0, 0, 0, 0, 0, 0
    .byte   0, 0, 0, 0, 0, 0, 0, 0
source_overlap_expected:
    .byte   20, 0, 0, 0, 0, 0, 0, 0
    .byte   0, 0, 0, 0, 0, 0, 0, 0
inactive_expected:
    .byte   10, 0, 0, 0, 0, 0, 0, 0
    .byte   0, 0, 0, 0, 0, 0, 0, 0

    .data
    .balign 16
int_vl0_output:
    .space  16
fp_vl0_output:
    .space  16
int_lmul8_output:
    .space  16
fp_lmul8_output:
    .space  16
mask_overlap_output:
    .space  16
source_overlap_output:
    .space  16
inactive_output:
    .space  16
semiargs:
    .space  16
