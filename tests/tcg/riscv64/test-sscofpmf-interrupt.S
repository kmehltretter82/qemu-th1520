/*
 * Sscofpmf machine and delegated supervisor interrupt test.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ IRQ_LCOFI,          13
    .equ LCOFI,              1 << IRQ_LCOFI
    .equ HPMCOUNTER3,        1 << 3
    .equ EVENT_INSTRUCTIONS, 2
    .equ MSTATUS_MIE,        1 << 3
    .equ MSTATUS_MPP,        3 << 11
    .equ MSTATUS_MPP_S,      1 << 11
    .equ SSTATUS_SIE,        1 << 1

    .text
    .global _start
_start:
    lla     t0, m_trap
    csrw    mtvec, t0

    /* Arm an M-mode instruction counter overflow. */
    li      t0, HPMCOUNTER3
    csrs    mcountinhibit, t0
    li      t0, EVENT_INSTRUCTIONS
    csrw    mhpmevent3, t0
    li      t0, -0x10000
    csrw    mhpmcounter3, t0
    li      t0, LCOFI
    csrc    mip, t0
    csrs    mie, t0
    csrr    t1, mie
    and     t1, t1, t0
    beqz    t1, fail_mie
    li      t1, MSTATUS_MIE
    csrs    mstatus, t1
    li      t1, HPMCOUNTER3
    csrc    mcountinhibit, t1

    li      t0, 0x1000000
1:
    addi    t0, t0, -1
    bnez    t0, 1b
    li      a0, 2
    j       exit

m_trap:
    csrr    t0, mcause
    li      t1, 9                  /* S-mode environment call */
    beq     t0, t1, exit
    li      t1, 1
    slli    t1, t1, 63
    ori     t1, t1, IRQ_LCOFI
    bne     t0, t1, fail_mcause

    csrr    t0, mhpmevent3
    bgez    t0, fail_m_of
    li      t0, HPMCOUNTER3
    csrs    mcountinhibit, t0
    li      t0, LCOFI
    csrc    mip, t0
    csrr    t1, mip
    and     t1, t1, t0
    bnez    t1, fail_m_ack

    /* Clear OF, delegate LCOFI and arm a second overflow for S-mode. */
    li      t0, EVENT_INSTRUCTIONS
    csrw    mhpmevent3, t0
    li      t0, LCOFI
    csrs    mideleg, t0
    csrr    t1, mideleg
    and     t1, t1, t0
    beqz    t1, fail_mideleg
    csrs    sie, t0
    csrr    t1, sie
    and     t1, t1, t0
    beqz    t1, fail_sie_alias
    csrr    t1, mie
    and     t1, t1, t0
    beqz    t1, fail_sie_alias

    lla     t1, s_trap
    csrw    stvec, t1
    li      t1, -0x10000
    csrw    mhpmcounter3, t1
    li      t1, SSTATUS_SIE
    csrs    sstatus, t1
    lla     t1, s_mode
    csrw    mepc, t1
    csrr    t1, mstatus
    li      t2, ~MSTATUS_MPP
    and     t1, t1, t2
    li      t2, MSTATUS_MPP_S
    or      t1, t1, t2
    csrw    mstatus, t1
    li      t1, HPMCOUNTER3
    csrc    mcountinhibit, t1
    mret

s_mode:
    li      t0, LCOFI
    csrr    t1, sie
    and     t1, t1, t0
    beqz    t1, fail_sie
    li      t0, 0x1000000
2:
    addi    t0, t0, -1
    bnez    t0, 2b
    li      a0, 9
    ecall

s_trap:
    csrr    t0, scause
    li      t1, 1
    slli    t1, t1, 63
    ori     t1, t1, IRQ_LCOFI
    bne     t0, t1, fail_scause
    li      t0, LCOFI
    csrr    t1, sip
    and     t1, t1, t0
    beqz    t1, fail_sip
    csrc    sip, t0
    csrr    t1, sip
    and     t1, t1, t0
    bnez    t1, fail_s_ack
    li      a0, 0
    ecall

fail_mie:
    li      a0, 1
    j       exit
fail_mcause:
    li      a0, 3
    j       exit
fail_m_of:
    li      a0, 4
    j       exit
fail_m_ack:
    li      a0, 5
    j       exit
fail_mideleg:
    li      a0, 6
    j       exit
fail_sie_alias:
    li      a0, 7
    j       exit
fail_sie:
    li      a0, 8
    ecall
fail_scause:
    li      a0, 10
    ecall
fail_sip:
    li      a0, 11
    ecall
fail_s_ack:
    li      a0, 12
    ecall

exit:
    lla     a1, semiargs
    sd      a0, 8(a1)
    li      t0, 0x20026
    sd      t0, 0(a1)
    li      a0, 0x20

    .balign 16
    slli    zero, zero, 0x1f
    ebreak
    srai    zero, zero, 0x7
    j       .

    .data
    .balign 16
semiargs:
    .space  16
