/*
 * XTheadVector implicit mask-source overlap test.
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
    bne     s1, t0, fail_missing_trap
    .endm

    .text
    .global _start
_start:
    csrr    t0, mhartid
    bnez    t0, secondary_hart
    lla     t0, trap
    csrw    mtvec, t0
    li      t0, MSTATUS_FS_INITIAL | MSTATUS_VS_THEAD
    csrs    mstatus, t0
    li      s0, 1             /* Expect illegal-instruction traps. */
    li      s1, 0             /* Count accepted traps. */

    /*
     * Seed v0 so the first illegal comparison can prove no destination write.
     */
    li      t1, 16
    .word   0x00037057        /* th.vsetvli zero, t1, e8, m1 */
    lla     t2, v0_input
    .word   0x12038007        /* th.vlb.v v0, (t2) */

    /* A masked comparison destination may not overlap v0 when LMUL > 1. */
    li      t1, 4
    .word   0x00137057        /* th.vsetvli zero, t1, e8, m2 */
    li      t0, 1
    csrw    0x008, t0         /* th.vstart must survive the illegal trap. */
    EXPECT_ILLEGAL 0x60430057, 1 /* th.vmseq.vv v0, v4, v6, v0.t */

    csrr    t0, 0x008
    li      t2, 1
    bne     t0, t2, fail_vstart_sentinel
    csrw    0x008, zero
    li      t1, 16
    .word   0x00037057        /* th.vsetvli zero, t1, e8, m1 */
    lla     t3, v0_output
    .word   0x020e0027        /* th.vsb.v v0, (t3) */
    lla     t2, v0_input
    ld      t4, 0(t2)
    ld      t5, 0(t3)
    bne     t4, t5, fail_v0
    ld      t4, 8(t2)
    ld      t5, 8(t3)
    bne     t4, t5, fail_v0

    li      t1, 4
    .word   0x00137057        /* th.vsetvli zero, t1, e8, m2 */
    EXPECT_ILLEGAL 0x60434057, 2 /* th.vmseq.vx v0, v4, t1, v0.t */
    EXPECT_ILLEGAL 0x6041b057, 3 /* th.vmseq.vi v0, v4, 3, v0.t */

    /* Floating-point comparisons have the same implicit mask source. */
    li      t1, 4
    .word   0x00937057        /* th.vsetvli zero, t1, e32, m2 */
    EXPECT_ILLEGAL 0x60431057, 4 /* th.vmfeq.vv v0, v4, v6, v0.t */
    EXPECT_ILLEGAL 0x60405057, 5 /* th.vmfeq.vf v0, v4, ft0, v0.t */

    /* Mask-prefix destinations follow the inherited masked-overlap rule. */
    li      t1, 4
    .word   0x00137057        /* th.vsetvli zero, t1, e8, m2 */
    EXPECT_ILLEGAL 0x5820a057, 6 /* th.vmsbf.m v0, v2, v0.t */
    EXPECT_ILLEGAL 0x5821a057, 7 /* th.vmsif.m v0, v2, v0.t */
    EXPECT_ILLEGAL 0x58212057, 8 /* th.vmsof.m v0, v2, v0.t */

    csrr    t0, 0x008         /* Illegal instructions preserve th.vstart. */
    bnez    t0, fail_vstart

    /* LMUL=1 permits masked destination overlap with v0. */
    li      s0, 0
    li      t1, 4
    .word   0x00037057        /* th.vsetvli zero, t1, e8, m1 */
    .word   0x60430057        /* th.vmseq.vv v0, v4, v6, v0.t */
    .word   0x60434057        /* th.vmseq.vx v0, v4, t1, v0.t */
    .word   0x6041b057        /* th.vmseq.vi v0, v4, 3, v0.t */

    .word   0x00837057        /* th.vsetvli zero, t1, e32, m1 */
    .word   0x60431057        /* th.vmfeq.vv v0, v4, v6, v0.t */
    .word   0x60405057        /* th.vmfeq.vf v0, v4, ft0, v0.t */

    .word   0x00037057        /* th.vsetvli zero, t1, e8, m1 */
    .word   0x5820a057        /* th.vmsbf.m v0, v2, v0.t */

    /* An unmasked LMUL=2 comparison has no implicit v0 source. */
    .word   0x00137057        /* th.vsetvli zero, t1, e8, m2 */
    .word   0x62430057        /* th.vmseq.vv v0, v4, v6 */

    /* A non-v0 destination remains legal for a masked LMUL=2 operation. */
    .word   0x60430457        /* th.vmseq.vv v8, v4, v6, v0.t */
    .word   0x5820a257        /* th.vmsbf.m v4, v2, v0.t */

    li      t0, 8
    bne     s1, t0, fail_trap_count
    li      a0, 0
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

fail_missing_trap:
    mv      a0, t0            /* 1..8 identify the missing trap. */
    j       exit
fail_vstart_sentinel: li a0, 9;  j exit
fail_v0:              li a0, 10; j exit
fail_vstart:          li a0, 11; j exit
fail_trap_count:      li a0, 12; j exit
fail_bad_cause:       li a0, 90; j exit
fail_unexpected_trap: li a0, 91; j exit

secondary_hart:
    wfi
    j       secondary_hart

exit:
    lla     a1, semiargs
    sd      a0, 8(a1)
    li      t0, 0x20026       /* ADP_Stopped_ApplicationExit */
    sd      t0, 0(a1)
    li      a0, 0x20          /* TARGET_SYS_EXIT_EXTENDED */

    .balign 16
    slli    zero, zero, 0x1f
    ebreak
    srai    zero, zero, 0x7
    j       .

    .data
    .balign 16
v0_input:
    .byte   0x81, 0x42, 0x24, 0x18, 0xa5, 0x5a, 0xc3, 0x3c
    .byte   0x7e, 0xe7, 0x11, 0x88, 0x69, 0x96, 0xbd, 0xdb
v0_output:
    .space  16
semiargs:
    .space  16
