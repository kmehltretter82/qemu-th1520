/*
 * T-Head C910 pre-Sscofpmf counter-overflow interrupt test.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ CSR_TH_MCOUNTERINTEN, 0x7ca
    .equ CSR_TH_MCOUNTEROF,    0x7cb
    .equ IRQ_THEAD_COUNTER_OF, 17

    .text
    .global _start
_start:
    lla     t0, trap
    csrw    mtvec, t0

    /* Program the fixed cycle counter close to wrap while inhibited. */
    li      t0, 1
    csrw    mcountinhibit, t0
    li      t0, -256
    csrw    mcycle, t0
    csrw    CSR_TH_MCOUNTEROF, zero

    /* Enable the C9xx overflow source and its local machine interrupt. */
    li      t0, 1
    csrw    CSR_TH_MCOUNTERINTEN, t0
    li      t0, 1
    slli    t0, t0, IRQ_THEAD_COUNTER_OF
    csrs    mie, t0
    li      t0, 8
    csrs    mstatus, t0

    /* Starting mcycle also arms QEMU's overflow timer. */
    csrw    mcountinhibit, zero

    li      t0, 0x100000
1:
    addi    t0, t0, -1
    bnez    t0, 1b
    li      a0, 1
    j       exit

trap:
    csrr    t0, mcause
    li      t1, 1
    slli    t1, t1, 63
    ori     t1, t1, IRQ_THEAD_COUNTER_OF
    bne     t0, t1, fail_cause

    csrr    t0, CSR_TH_MCOUNTEROF
    andi    t0, t0, 1
    beqz    t0, fail_overflow

    /* Stop the source, acknowledge it, and require MIP to deassert. */
    li      t0, 1
    csrw    mcountinhibit, t0
    csrw    CSR_TH_MCOUNTERINTEN, zero
    csrw    CSR_TH_MCOUNTEROF, zero
    csrr    t0, mip
    li      t1, 1
    slli    t1, t1, IRQ_THEAD_COUNTER_OF
    and     t0, t0, t1
    bnez    t0, fail_ack

    li      a0, 0
    j       exit

fail_cause:
    li      a0, 2
    j       exit
fail_overflow:
    li      a0, 3
    j       exit
fail_ack:
    li      a0, 4

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
