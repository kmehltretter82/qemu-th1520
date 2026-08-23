/*
 * T-Head C900 CLINT end-to-end interrupt delivery test.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ C900_CLINT_BASE, 0xffdc000000
    .equ C900_MSIP,       0x0000
    .equ C900_MTIMECMP,   0x4000
    .equ C900_SSIP,       0xc000
    .equ C900_STIMECMP,   0xd000

    .equ MIP_SSIP,        (1 << 1)
    .equ MIP_MSIP,        (1 << 3)
    .equ MIP_STIP,        (1 << 5)
    .equ MIP_MTIP,        (1 << 7)

    .text
    .global _start
_start:
    csrr    t0, mhartid
    bnez    t0, secondary
    lla     t0, machine_trap
    csrw    mtvec, t0
    li      t0, C900_CLINT_BASE

    /* Machine software interrupt: only bit zero is implemented. */
    li      t1, -1
    sw      t1, C900_MSIP(t0)
    fence   iorw, iorw
    lw      t2, C900_MSIP(t0)
    li      t3, 1
    bne     t2, t3, fail_msip_register
    csrr    t2, mip
    andi    t2, t2, MIP_MSIP
    beqz    t2, fail_msip_raise
    sw      zero, C900_MSIP(t0)
    fence   iorw, iorw
    csrr    t2, mip
    andi    t2, t2, MIP_MSIP
    bnez    t2, fail_msip_lower

    /* Supervisor software interrupt is a distinct C900 register bank. */
    li      t3, C900_SSIP
    add     t3, t0, t3
    sw      t1, 0(t3)
    fence   iorw, iorw
    lw      t2, 0(t3)
    li      t4, 1
    bne     t2, t4, fail_ssip_register
    csrr    t2, mip
    andi    t2, t2, MIP_SSIP
    beqz    t2, fail_ssip_raise
    sw      zero, 0(t3)
    fence   iorw, iorw
    csrr    t2, mip
    andi    t2, t2, MIP_SSIP
    bnez    t2, fail_ssip_lower

    /* The APB timer compare banks are written as two 32-bit halves. */
    li      t3, C900_MTIMECMP
    add     t3, t0, t3
    sw      zero, 4(t3)
    sw      zero, 0(t3)
    fence   iorw, iorw
    csrr    t2, mip
    andi    t2, t2, MIP_MTIP
    beqz    t2, fail_mtimer_raise
    sw      t1, 4(t3)
    sw      t1, 0(t3)
    fence   iorw, iorw
    csrr    t2, mip
    andi    t2, t2, MIP_MTIP
    bnez    t2, fail_mtimer_lower

    li      t3, C900_STIMECMP
    add     t3, t0, t3
    sw      zero, 4(t3)
    sw      zero, 0(t3)
    fence   iorw, iorw
    csrr    t2, mip
    andi    t2, t2, MIP_STIP
    beqz    t2, fail_stimer_raise
    sw      t1, 4(t3)
    sw      t1, 0(t3)
    fence   iorw, iorw
    csrr    t2, mip
    andi    t2, t2, MIP_STIP
    bnez    t2, fail_stimer_lower

    /*
     * The C900 exposes its S banks in S-mode but retains the M banks for
     * M-mode.  Delegate the S pending bits so sip provides a useful check.
     */
    lla     t0, expected_fault
    sd      zero, 0(t0)
    lla     t0, fault_seen
    sd      zero, 0(t0)
    li      t0, MIP_SSIP | MIP_STIP
    csrw    mideleg, t0

    csrr    t0, mstatus
    li      t1, ~(3 << 11)
    and     t0, t0, t1
    li      t1, (1 << 11)       /* MPP = S */
    or      t0, t0, t1
    csrw    mstatus, t0
    lla     t0, supervisor_test
    csrw    mepc, t0
    mret

secondary:
    wfi
    j       secondary

supervisor_test:
    li      t0, C900_CLINT_BASE
    li      t1, -1

    li      t3, C900_SSIP
    add     t3, t0, t3
    sw      t1, 0(t3)
    fence   iorw, iorw
    lw      t2, 0(t3)
    li      t4, 1
    bne     t2, t4, fail_s_ssip_register
    csrr    t2, sip
    andi    t2, t2, MIP_SSIP
    beqz    t2, fail_s_ssip_raise
    sw      zero, 0(t3)
    fence   iorw, iorw
    csrr    t2, sip
    andi    t2, t2, MIP_SSIP
    bnez    t2, fail_s_ssip_lower

    li      t3, C900_STIMECMP
    add     t3, t0, t3
    sw      zero, 4(t3)
    sw      zero, 0(t3)
    fence   iorw, iorw
    csrr    t2, sip
    andi    t2, t2, MIP_STIP
    beqz    t2, fail_s_stimer_raise
    sw      t1, 4(t3)
    sw      t1, 0(t3)
    fence   iorw, iorw
    csrr    t2, sip
    andi    t2, t2, MIP_STIP
    bnez    t2, fail_s_stimer_lower

    /* This M-bank write must trap back to M-mode with an access fault. */
    lla     t2, expected_fault
    li      t3, 1
    sd      t3, 0(t2)
    sw      t3, C900_MSIP(t0)
    lla     t2, fault_seen
    ld      t2, 0(t2)
    bne     t2, t3, fail_s_msip_access

    li      a0, 0
    ecall

machine_trap:
    csrr    t0, mcause
    li      t1, 7             /* Store/AMO access fault */
    beq     t0, t1, machine_store_fault
    li      t1, 9             /* Environment call from S-mode */
    beq     t0, t1, exit
    li      a0, 17
    j       exit

machine_store_fault:
    lla     t0, expected_fault
    ld      t1, 0(t0)
    li      t2, 1
    bne     t1, t2, fail_unexpected_access
    lla     t0, fault_seen
    sd      t2, 0(t0)
    csrr    t0, mepc
    addi    t0, t0, 4
    csrw    mepc, t0
    mret

fail_msip_register:
    li      a0, 1
    j       exit
fail_msip_raise:
    li      a0, 2
    j       exit
fail_msip_lower:
    li      a0, 3
    j       exit
fail_ssip_register:
    li      a0, 4
    j       exit
fail_ssip_raise:
    li      a0, 5
    j       exit
fail_ssip_lower:
    li      a0, 6
    j       exit
fail_mtimer_raise:
    li      a0, 7
    j       exit
fail_mtimer_lower:
    li      a0, 8
    j       exit
fail_stimer_raise:
    li      a0, 9
    j       exit
fail_stimer_lower:
    li      a0, 10
    j       exit
fail_s_ssip_register:
    li      a0, 11
    ecall
fail_s_ssip_raise:
    li      a0, 12
    ecall
fail_s_ssip_lower:
    li      a0, 13
    ecall
fail_s_stimer_raise:
    li      a0, 14
    ecall
fail_s_stimer_lower:
    li      a0, 15
    ecall
fail_s_msip_access:
    li      a0, 16
    ecall
fail_unexpected_access:
    li      a0, 18

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
expected_fault:
    .dword  0
fault_seen:
    .dword  0
