/*
 * XTheadVector floating-point state test.
 *
 * The vector instructions are emitted as words so this test does not require
 * an assembler with XTheadVector support.  Encodings come from the frozen
 * XTheadVector specification and target/riscv/xtheadvector.decode.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ MSTATUS_FS_INITIAL,  (1 << 13)
    .equ MSTATUS_FS_MASK,     (3 << 13)
    .equ MSTATUS_FS_DIRTY,    (3 << 13)
    .equ MSTATUS_VS_THEAD,    (1 << 23)
    .equ FFLAGS_NX,           (1 << 0)
    .equ FFLAGS_OF,           (1 << 2)
    .equ FFLAGS_DZ,           (1 << 3)
    .equ FFLAGS_NV,           (1 << 4)

    .macro EXPECT_ILLEGAL encoding, count
    .word   \encoding
    li      t0, \count
    bne     s1, t0, fail_illegal
    .endm

    .macro RESET_FP_STATE
    li      t0, MSTATUS_FS_INITIAL
    csrs    mstatus, t0
    csrw    fflags, zero
    li      t0, MSTATUS_FS_MASK
    csrc    mstatus, t0
    li      t0, MSTATUS_FS_INITIAL
    csrs    mstatus, t0
    .endm

    /* One valid representative for every floating-point decode check. */
    .macro EXEC_FP_DECODE_MATRIX
    .word   0x821111d7        /* th.vfdiv.vv v3, v1, v2 */
    .word   0x821051d7        /* th.vfdiv.vf v3, v1, ft0 */
    .word   0xc2111257        /* th.vfwadd.vv v4, v1, v2 */
    .word   0xc2105257        /* th.vfwadd.vf v4, v1, ft0 */
    .word   0xd2409357        /* th.vfwadd.wv v6, v4, v1 */
    .word   0xd2405357        /* th.vfwadd.wf v6, v4, ft0 */
    .word   0x8e4011d7        /* th.vfsqrt.v v3, v4 */
    .word   0x624092d7        /* th.vmfeq.vv v5, v4, v1 */
    .word   0x624052d7        /* th.vmfeq.vf v5, v4, ft0 */
    .word   0x5e0051d7        /* th.vfmv.v.f v3, ft0 */
    .word   0x8a141257        /* th.vfwcvt.xu.f.v v4, v1 */
    .word   0x8a151257        /* th.vfwcvt.f.xu.v v4, v1 */
    .word   0x8a4910d7        /* th.vfncvt.f.xu.v v1, v4 */
    .word   0x8a4810d7        /* th.vfncvt.xu.f.v v1, v4 */
    .word   0x064091d7        /* th.vfredsum.vs v3, v4, v1 */
    .word   0xc6111257        /* th.vfwredsum.vs v4, v1, v2 */
    .word   0x321010d7        /* th.vfmv.f.s ft1, v1 */
    .word   0x360051d7        /* th.vfmv.s.f v3, ft0 */
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

    /* Prepare one 32-bit numerator and denominator while FS is enabled. */
    li      t0, MSTATUS_FS_INITIAL | MSTATUS_VS_THEAD
    csrs    mstatus, t0
    li      t1, 1
    .word   0x00837057        /* th.vsetvli zero, t1, e32, m1 */
    lla     t2, numerator
    .word   0x0203f087        /* th.vle.v v1, (t2) */
    lla     t2, denominator
    .word   0x0203f107        /* th.vle.v v2, (t2) */

    /* Every vector floating-point instruction is illegal while FS is Off. */
    li      t0, MSTATUS_FS_MASK
    csrc    mstatus, t0
    li      s0, 1

    EXPECT_ILLEGAL 0x821111d7, 1
    EXPECT_ILLEGAL 0x821051d7, 2
    EXPECT_ILLEGAL 0xc2111257, 3
    EXPECT_ILLEGAL 0xc2105257, 4
    EXPECT_ILLEGAL 0xd2409357, 5
    EXPECT_ILLEGAL 0xd2405357, 6
    EXPECT_ILLEGAL 0x8e4011d7, 7
    EXPECT_ILLEGAL 0x624092d7, 8
    EXPECT_ILLEGAL 0x624052d7, 9
    EXPECT_ILLEGAL 0x5e0051d7, 10
    EXPECT_ILLEGAL 0x8a141257, 11
    EXPECT_ILLEGAL 0x8a151257, 12
    EXPECT_ILLEGAL 0x8a4910d7, 13
    EXPECT_ILLEGAL 0x8a4810d7, 14
    EXPECT_ILLEGAL 0x064091d7, 15
    EXPECT_ILLEGAL 0xc6111257, 16
    EXPECT_ILLEGAL 0x321010d7, 17
    EXPECT_ILLEGAL 0x360051d7, 18

    /* Reduction helpers have no 8-bit FP form or 64-to-128 widening form. */
    li      t0, MSTATUS_FS_INITIAL
    csrs    mstatus, t0
    li      t1, 1
    .word   0x00037057        /* th.vsetvli zero, t1, e8, m1 */
    EXPECT_ILLEGAL 0x064091d7, 19
    .word   0x00c37057        /* th.vsetvli zero, t1, e64, m1 */
    EXPECT_ILLEGAL 0xc6111257, 20

    /* Each matrix encoding must also decode when FS is enabled. */
    li      s0, 0
    li      t1, 0
    .word   0x00837057        /* th.vsetvli zero, t1, e32, m1 */
    EXEC_FP_DECODE_MATRIX

    /* Restore one active element and the operands used by the state tests. */
    li      t1, 1
    .word   0x00837057
    lla     t2, numerator
    .word   0x0203f087        /* th.vle.v v1, (t2) */
    lla     t2, denominator
    .word   0x0203f107        /* th.vle.v v2, (t2) */

    /*
     * A vector result alone dirties VS, not FS.  With no new floating-point
     * exception, FS must remain Initial.
     */
    RESET_FP_STATE
    .word   0x821091d7        /* th.vfdiv.vv v3, v1, v1 */
    csrr    t0, fflags
    bnez    t0, fail_no_flags
    csrr    t0, mstatus
    li      t1, MSTATUS_FS_MASK
    and     t0, t0, t1
    li      t1, MSTATUS_FS_INITIAL
    bne     t0, t1, fail_clean_dirty

    /* A newly accrued divide-by-zero flag modifies fcsr and dirties FS. */
    RESET_FP_STATE
    .word   0x821111d7        /* th.vfdiv.vv v3, v1, v2 */
    csrr    t0, fflags
    li      t1, FFLAGS_DZ
    bne     t0, t1, fail_vv_flags
    csrr    t0, mstatus
    li      t1, MSTATUS_FS_MASK
    and     t0, t0, t1
    li      t1, MSTATUS_FS_DIRTY
    bne     t0, t1, fail_vv_dirty

    /* Also prove that the operation produced positive infinity. */
    lla     t2, result
    .word   0x0203f1a7        /* th.vse.v v3, (t2) */
    lw      t0, 0(t2)
    li      t1, 0x7f800000
    bne     t0, t1, fail_result

    /* Vector-scalar arithmetic uses the independent GEN_TH_VF helper. */
    lla     t2, denominator
    flw     ft0, 0(t2)
    RESET_FP_STATE
    .word   0x821051d7        /* th.vfdiv.vf v3, v1, ft0 */
    li      a2, FFLAGS_DZ
    li      a3, 50
    call    check_fp_dirty

    /* Unary arithmetic must propagate an invalid-operation exception. */
    lla     t2, negative_one
    .word   0x0203f207        /* th.vle.v v4, (t2) */
    RESET_FP_STATE
    .word   0x8e4011d7        /* th.vfsqrt.v v3, v4 */
    li      a2, FFLAGS_NV
    li      a3, 51
    call    check_fp_dirty

    /* Signaling-NaN vector-vector and vector-scalar comparisons accrue NV. */
    lla     t2, signaling_nan
    .word   0x0203f207        /* th.vle.v v4, (t2) */
    RESET_FP_STATE
    .word   0x624092d7        /* th.vmfeq.vv v5, v4, v1 */
    li      a2, FFLAGS_NV
    li      a3, 52
    call    check_fp_dirty

    RESET_FP_STATE
    .word   0x624052d7        /* th.vmfeq.vf v5, v4, ft0 */
    li      a2, FFLAGS_NV
    li      a3, 53
    call    check_fp_dirty

    /* A maximum-finite reduction overflows and becomes inexact. */
    lla     t2, max_finite
    .word   0x0203f087        /* th.vle.v v1, (t2) */
    .word   0x0203f207        /* th.vle.v v4, (t2) */
    RESET_FP_STATE
    .word   0x064091d7        /* th.vfredsum.vs v3, v4, v1 */
    li      a2, FFLAGS_OF | FFLAGS_NX
    li      a3, 54
    call    check_fp_dirty

    li      a0, 0
    j       exit

check_fp_dirty:
    csrr    t0, fflags
    bne     t0, a2, check_fp_failed
    csrr    t0, mstatus
    li      t1, MSTATUS_FS_MASK
    and     t0, t0, t1
    li      t1, MSTATUS_FS_DIRTY
    bne     t0, t1, check_fp_failed
    ret
check_fp_failed:
    mv      a0, a3
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
    addi    a0, t0, 20      /* 21..40 identify the missing trap. */
    j       exit
fail_no_flags:       li a0, 41; j exit
fail_clean_dirty:    li a0, 42; j exit
fail_vv_flags:       li a0, 43; j exit
fail_vv_dirty:       li a0, 44; j exit
fail_result:         li a0, 45; j exit
fail_bad_cause:      li a0, 90; j exit
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
numerator:
    .word   0x3f800000      /* 1.0f */
    .space  12
denominator:
    .word   0x00000000      /* 0.0f */
    .space  12
negative_one:
    .word   0xbf800000      /* -1.0f */
    .space  12
signaling_nan:
    .word   0x7f800001
    .space  12
max_finite:
    .word   0x7f7fffff
    .space  12

    .data
    .balign 16
result:
    .space  16
semiargs:
    .space  16
