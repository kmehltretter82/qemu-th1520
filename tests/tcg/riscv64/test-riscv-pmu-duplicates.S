/*
 * RISC-V PMU duplicate event-selector test.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ PMU_EVENT_INSTRUCTIONS, 2
    .equ PMU_COUNTER_MASK,       0x18

    .text
    .global _start
_start:
    lla     t0, unexpected_trap
    csrw    mtvec, t0

    /* Configure counters 3 and 4 while both are inhibited. */
    li      t0, PMU_COUNTER_MASK
    csrw    mcountinhibit, t0
    csrw    mhpmcounter3, zero
    csrw    mhpmcounter4, zero
    li      t1, PMU_EVENT_INSTRUCTIONS
    csrw    mhpmevent3, t1
    csrw    mhpmevent4, t1

    /* Both counters selecting the same event must advance together. */
    csrc    mcountinhibit, t0
    li      t2, 64
1:
    addi    t2, t2, -1
    bnez    t2, 1b
    csrs    mcountinhibit, t0

    csrr    t3, mhpmcounter3
    csrr    t4, mhpmcounter4
    beqz    t3, fail_counter3
    beqz    t4, fail_counter4
    bne     t3, t4, fail_mismatch

    /* Removing counter 3 must preserve counter 4's duplicate mapping. */
    csrw    mhpmevent3, zero
    csrw    mhpmcounter3, zero
    csrw    mhpmcounter4, zero
    csrc    mcountinhibit, t0
    li      t2, 64
2:
    addi    t2, t2, -1
    bnez    t2, 2b
    csrs    mcountinhibit, t0

    csrr    t3, mhpmcounter3
    csrr    t4, mhpmcounter4
    bnez    t3, fail_removed
    beqz    t4, fail_survivor

    li      a0, 0
    j       exit

fail_counter3:
    li      a0, 1
    j       exit
fail_counter4:
    li      a0, 2
    j       exit
fail_mismatch:
    li      a0, 3
    j       exit
fail_removed:
    li      a0, 4
    j       exit
fail_survivor:
    li      a0, 5
    j       exit
unexpected_trap:
    li      a0, 6

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
