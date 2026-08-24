/*
 * RVV widening operations must reject an e64 source element width.
 *
 * These instructions are emitted as words so that the test does not depend on
 * a particular assembler version.  The same four instruction forms exercise
 * every three-entry widening helper dispatch table in the translator.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .macro EXPECT_ILLEGAL encoding, count
    .word   \encoding
    li      t0, \count
    bne     s1, t0, fail
    .endm

    .text
    .global _start
_start:
    lla     t0, trap
    csrw    mtvec, t0
    li      s1, 0

    /* mstatus.VS = Dirty. */
    li      t0, 0x600
    csrs    mstatus, t0

    li      t1, 1
    .word   0x0d837057        /* vsetvli zero, t1, e64, m1, ta, ma */

    /* No 64-to-128-bit helper exists for any widening source form. */
    EXPECT_ILLEGAL 0xc21101d7, 1 /* vwredsumu.vs v3, v1, v2 */
    EXPECT_ILLEGAL 0xc220a1d7, 2 /* vwaddu.vv v3, v1, v2 */
    EXPECT_ILLEGAL 0xd220a1d7, 3 /* vwaddu.wv v3, v1, v2 */
    EXPECT_ILLEGAL 0xd220e1d7, 4 /* vwaddu.wx v3, v1, t2 */

    li      a0, 0
    j       exit

trap:
    csrr    t0, mcause
    li      t1, 2
    bne     t0, t1, fail
    addi    s1, s1, 1
    csrr    t0, mepc
    addi    t0, t0, 4
    csrw    mepc, t0
    mret

fail:
    li      a0, 1

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

    .data
    .balign 16
semiargs:
    .space  16
