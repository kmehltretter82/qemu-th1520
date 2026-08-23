/*
 * T-Head C900 PLIC privilege and end-to-end delivery test.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ C900_PLIC_BASE,       0xffd8000000
    .equ PLIC_PRIORITY_0,      0x000000
    .equ PLIC_PRIORITY_120,    0x0001e0
    .equ PLIC_PENDING_3_ADDR,  (C900_PLIC_BASE + 0x00100c)
    .equ PLIC_M_ENABLE_3_ADDR, (C900_PLIC_BASE + 0x00200c)
    .equ PLIC_S_ENABLE_3_ADDR, (C900_PLIC_BASE + 0x00208c)
    .equ PLIC_CONTROL_ADDR,    (C900_PLIC_BASE + 0x1ffffc)
    .equ PLIC_M_THRESHOLD_ADDR, (C900_PLIC_BASE + 0x200000)
    .equ PLIC_M_CLAIM_ADDR,    (C900_PLIC_BASE + 0x200004)
    .equ PLIC_S_THRESHOLD_ADDR, (C900_PLIC_BASE + 0x201000)
    .equ PLIC_S_CLAIM_ADDR,    (C900_PLIC_BASE + 0x201004)

    .equ IRQ_ID,               120
    .equ IRQ_WORD_BIT,         (1 << 24)
    .equ MIP_SEIP,             (1 << 9)
    .equ MIP_MEIP,             (1 << 11)

    .text
    .global _start
_start:
    csrr    t0, mhartid
    bnez    t0, secondary
    lla     t0, machine_trap
    csrw    mtvec, t0

    /* The C900 PLIC is a 32-bit APB slave; a 64-bit transaction faults. */
    lla     t0, expected_fault
    li      t1, 4
    sd      t1, 0(t0)
    lla     t0, fault_seen
    sd      zero, 0(t0)
    li      t0, C900_PLIC_BASE
    sd      zero, PLIC_PRIORITY_120(t0)
    lla     t0, fault_seen
    ld      t0, 0(t0)
    bne     t0, t1, fail_m_width

    li      t0, C900_PLIC_BASE

    /* Exercise the machine context and the CPU's MEIP input first. */
    li      t1, 5
    sw      t1, PLIC_PRIORITY_120(t0)
    li      t1, IRQ_WORD_BIT
    li      t4, PLIC_M_ENABLE_3_ADDR
    sw      t1, 0(t4)
    li      t4, PLIC_PENDING_3_ADDR
    sw      t1, 0(t4)
    fence   iorw, iorw
    csrr    t2, mip
    li      t3, MIP_MEIP
    and     t2, t2, t3
    beqz    t2, fail_m_raise
    li      t4, PLIC_M_CLAIM_ADDR
    lw      t2, 0(t4)
    li      t3, IRQ_ID
    bne     t2, t3, fail_m_claim
    fence   iorw, iorw
    csrr    t2, mip
    li      t5, MIP_MEIP
    and     t2, t2, t5
    bnez    t2, fail_m_lower
    sw      t3, 0(t4)
    li      t4, PLIC_M_ENABLE_3_ADDR
    sw      zero, 0(t4)

    /* Let sip expose the supervisor external-interrupt input. */
    li      t1, MIP_SEIP
    csrw    mideleg, t1
    lla     t1, expected_fault
    sd      zero, 0(t1)
    lla     t1, fault_seen
    sd      zero, 0(t1)

    csrr    t1, mstatus
    li      t2, ~(3 << 11)
    and     t1, t1, t2
    li      t2, (1 << 11)       /* MPP = S */
    or      t1, t1, t2
    csrw    mstatus, t1
    lla     t1, supervisor_test
    csrw    mepc, t1
    mret

secondary:
    wfi
    j       secondary

supervisor_test:
    li      t0, C900_PLIC_BASE
    li      t1, IRQ_WORD_BIT

    /* S-context registers work before the global delegation bit is set. */
    li      t5, PLIC_S_ENABLE_3_ADDR
    sw      t1, 0(t5)
    li      t5, PLIC_S_THRESHOLD_ADDR
    sw      zero, 0(t5)
    lw      t2, 0(t5)
    bnez    t2, fail_s_context_before_delegate

    /* Priority remains M-only; the trap handler delegates and resumes us. */
    lla     t2, expected_fault
    li      t3, 1
    sd      t3, 0(t2)
    sw      t3, PLIC_PRIORITY_120(t0)
    lla     t2, fault_seen
    ld      t2, 0(t2)
    li      t3, 1
    bne     t2, t3, fail_s_priority_fault
    li      t0, C900_PLIC_BASE
    li      t1, IRQ_WORD_BIT

    /* Delegated priority and threshold registers have five WARL bits. */
    li      t3, -1
    sw      t3, PLIC_PRIORITY_120(t0)
    lw      t2, PLIC_PRIORITY_120(t0)
    li      t4, 31
    bne     t2, t4, fail_s_priority_warl
    sw      t3, PLIC_PRIORITY_0(t0)
    lw      t2, PLIC_PRIORITY_0(t0)
    bnez    t2, fail_source_zero
    li      t5, PLIC_S_THRESHOLD_ADDR
    sw      t3, 0(t5)
    lw      t2, 0(t5)
    bne     t2, t4, fail_s_threshold_warl
    sw      zero, 0(t5)

    /* Delegation promotes S-mode for the M-context address bank as well. */
    li      t3, 9
    li      t5, PLIC_M_THRESHOLD_ADDR
    sw      t3, 0(t5)
    lw      t2, 0(t5)
    bne     t2, t3, fail_s_mcontext_delegate
    sw      zero, 0(t5)

    /* Software-pending state drives SEIP and is masked while active. */
    li      t3, 7
    sw      t3, PLIC_PRIORITY_120(t0)
    li      t5, PLIC_PENDING_3_ADDR
    sw      t1, 0(t5)
    fence   iorw, iorw
    csrr    t2, sip
    andi    t2, t2, MIP_SEIP
    beqz    t2, fail_s_raise
    li      t5, PLIC_S_CLAIM_ADDR
    lw      t2, 0(t5)
    li      t3, IRQ_ID
    bne     t2, t3, fail_s_claim
    fence   iorw, iorw
    csrr    t2, sip
    andi    t2, t2, MIP_SEIP
    bnez    t2, fail_s_claim_lower

    li      t5, PLIC_PENDING_3_ADDR
    sw      t1, 0(t5)
    fence   iorw, iorw
    csrr    t2, sip
    andi    t2, t2, MIP_SEIP
    bnez    t2, fail_active_mask
    li      t5, PLIC_S_CLAIM_ADDR
    sw      t3, 0(t5)
    fence   iorw, iorw
    csrr    t2, sip
    andi    t2, t2, MIP_SEIP
    beqz    t2, fail_complete_unmask
    lw      t2, 0(t5)
    bne     t2, t3, fail_s_reclaim
    sw      t3, 0(t5)
    fence   iorw, iorw
    csrr    t2, sip
    andi    t2, t2, MIP_SEIP
    bnez    t2, fail_s_complete_lower

    /* The delegation register itself remains machine-only. */
    lla     t2, expected_fault
    li      t3, 3
    sd      t3, 0(t2)
    lla     t2, fault_seen
    sd      zero, 0(t2)
    li      t5, PLIC_CONTROL_ADDR
    sw      zero, 0(t5)
    lla     t2, fault_seen
    ld      t2, 0(t2)
    li      t3, 3
    bne     t2, t3, fail_s_control_access

    /* U-mode has no PLIC aperture access, even after delegation. */
    lla     t2, expected_fault
    li      t3, 2
    sd      t3, 0(t2)
    lla     t2, fault_seen
    sd      zero, 0(t2)
    lla     t2, user_test
    csrw    sepc, t2
    csrr    t2, sstatus
    li      t4, ~(1 << 8)       /* SPP = U */
    and     t2, t2, t4
    csrw    sstatus, t2
    sret

user_test:
    li      t1, 1
    li      t0, PLIC_S_THRESHOLD_ADDR
    sw      t1, 0(t0)
    lla     t2, fault_seen
    ld      t2, 0(t2)
    li      t3, 2
    bne     t2, t3, fail_u_access
    li      a0, 0
    ecall

machine_trap:
    csrr    t0, mcause
    li      t1, 7               /* Store/AMO access fault */
    beq     t0, t1, machine_store_fault
    li      t1, 8               /* Environment call from U-mode */
    beq     t0, t1, exit
    li      t1, 9               /* Environment call from S-mode */
    beq     t0, t1, exit
    li      a0, 31
    j       exit

machine_store_fault:
    lla     t0, expected_fault
    ld      t1, 0(t0)
    beqz    t1, fail_unexpected_fault
    li      t2, 1
    bne     t1, t2, 1f
    li      t3, PLIC_CONTROL_ADDR
    sw      t2, 0(t3)
1:
    lla     t0, fault_seen
    sd      t1, 0(t0)
    csrr    t0, mepc
    addi    t0, t0, 4
    csrw    mepc, t0
    mret

fail_m_raise:
    li      a0, 1
    j       exit
fail_m_claim:
    li      a0, 2
    j       exit
fail_m_lower:
    li      a0, 3
    j       exit
fail_m_width:
    li      a0, 20
    j       exit
fail_s_context_before_delegate:
    li      a0, 4
    ecall
fail_s_priority_fault:
    li      a0, 5
    ecall
fail_s_priority_warl:
    li      a0, 6
    ecall
fail_source_zero:
    li      a0, 7
    ecall
fail_s_threshold_warl:
    li      a0, 8
    ecall
fail_s_mcontext_delegate:
    li      a0, 9
    ecall
fail_s_raise:
    li      a0, 10
    ecall
fail_s_claim:
    li      a0, 11
    ecall
fail_s_claim_lower:
    li      a0, 12
    ecall
fail_active_mask:
    li      a0, 13
    ecall
fail_complete_unmask:
    li      a0, 14
    ecall
fail_s_reclaim:
    li      a0, 15
    ecall
fail_s_complete_lower:
    li      a0, 16
    ecall
fail_s_control_access:
    li      a0, 17
    ecall
fail_u_access:
    li      a0, 18
    ecall
fail_unexpected_fault:
    li      a0, 19

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
