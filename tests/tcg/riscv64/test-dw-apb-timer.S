/*
 * DesignWare APB timer access and end-to-end interrupt delivery test.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ DW_TIMER_BASE,          0xffefc32000
    .equ DW_TIMER_LOAD_COUNT,    0x00
    .equ DW_TIMER_CURRENT_VALUE, 0x04
    .equ DW_TIMER_CONTROL,       0x08
    .equ DW_TIMER_EOI,           0x0c
    .equ DW_TIMER_INT_STATUS,    0x10
    .equ DW_TIMERS_INT_STATUS,   0xa0
    .equ DW_TIMERS_RAW_STATUS,   0xa8
    .equ DW_TIMERS_COMP_VERSION, 0xac

    .equ TIMER_ENABLE,           (1 << 0)
    .equ TIMER_INT_MASK,         (1 << 2)
    .equ TIMER_IRQ,              16

    .equ C900_PLIC_BASE,         0xffd8000000
    .equ PLIC_TIMER_PRIORITY,    (C900_PLIC_BASE + 0x40)
    .equ PLIC_M_ENABLE_0,        (C900_PLIC_BASE + 0x2000)
    .equ PLIC_M_CLAIM,           (C900_PLIC_BASE + 0x200004)
    .equ TIMER_IRQ_WORD_BIT,     (1 << TIMER_IRQ)
    .equ MIP_MEIP,               (1 << 11)

    .macro check_load_fault op
    lla     t0, expected_cause
    li      t1, 5               /* Load access fault */
    sd      t1, 0(t0)
    lla     t0, fault_seen
    sd      zero, 0(t0)
    \op     t2, 0(s0)
    lla     t0, fault_seen
    ld      t0, 0(t0)
    li      t1, 5
    bne     t0, t1, fail_access_width
    .endm

    .macro check_store_fault op
    lla     t0, expected_cause
    li      t1, 7               /* Store/AMO access fault */
    sd      t1, 0(t0)
    lla     t0, fault_seen
    sd      zero, 0(t0)
    \op     zero, 0(s0)
    lla     t0, fault_seen
    ld      t0, 0(t0)
    li      t1, 7
    bne     t0, t1, fail_access_width
    .endm

    .text
    .global _start
_start:
    csrr    t0, mhartid
    bnez    t0, secondary
    lla     t0, machine_trap
    csrw    mtvec, t0
    li      s0, DW_TIMER_BASE

    /* TH1520 exposes the timer as a 32-bit APB slave. */
    check_load_fault lb
    check_load_fault lh
    check_store_fault sb
    check_store_fault sh

    lw      t0, DW_TIMERS_COMP_VERSION(s0)
    li      t1, 0x3231322a
    bne     t0, t1, fail_component_version
    lwu     t0, DW_TIMER_CURRENT_VALUE(s0)
    li      t1, 0x80000000
    bne     t0, t1, fail_reset_value
    lw      t0, DW_TIMER_CONTROL(s0)
    bnez    t0, fail_reset_control

    /* Route timer 0's level interrupt to hart 0's machine context. */
    li      t0, PLIC_TIMER_PRIORITY
    li      t1, 5
    sw      t1, 0(t0)
    li      t0, PLIC_M_ENABLE_0
    li      t1, TIMER_IRQ_WORD_BIT
    sw      t1, 0(t0)

    /* First let the timer expire while its interrupt output is masked. */
    li      t0, 1000
    sw      t0, DW_TIMER_LOAD_COUNT(s0)
    li      t0, TIMER_ENABLE | TIMER_INT_MASK
    sw      t0, DW_TIMER_CONTROL(s0)
    li      t4, 1000000
1:
    lw      t0, DW_TIMERS_RAW_STATUS(s0)
    andi    t0, t0, 1
    bnez    t0, 2f
    addi    t4, t4, -1
    bnez    t4, 1b
    j       fail_timeout
2:
    lw      t0, DW_TIMER_INT_STATUS(s0)
    bnez    t0, fail_masked_status
    lw      t0, DW_TIMERS_INT_STATUS(s0)
    bnez    t0, fail_masked_status
    csrr    t0, mip
    li      t1, MIP_MEIP
    and     t0, t0, t1
    bnez    t0, fail_masked_raise

    /* Unmasking a latched event must propagate timer -> PLIC -> MEIP. */
    li      t0, TIMER_ENABLE
    sw      t0, DW_TIMER_CONTROL(s0)
    fence   iorw, iorw
    csrr    t0, mip
    li      t1, MIP_MEIP
    and     t0, t0, t1
    beqz    t0, fail_unmask_raise
    li      t0, PLIC_M_CLAIM
    lw      t1, 0(t0)
    li      t2, TIMER_IRQ
    bne     t1, t2, fail_claim
    lw      t1, DW_TIMER_INT_STATUS(s0)
    li      t3, 1
    bne     t1, t3, fail_local_status

    /* Reading EOI lowers the source; completing lowers the PLIC output. */
    lw      t1, DW_TIMER_EOI(s0)
    bnez    t1, fail_eoi_value
    sw      zero, DW_TIMER_CONTROL(s0)
    sw      t2, 0(t0)
    fence   iorw, iorw
    lw      t1, DW_TIMERS_RAW_STATUS(s0)
    bnez    t1, fail_eoi_clear
    csrr    t1, mip
    li      t2, MIP_MEIP
    and     t1, t1, t2
    bnez    t1, fail_complete_lower

    li      a0, 0
    j       exit

secondary:
    wfi
    j       secondary

machine_trap:
    csrr    t0, mcause
    lla     t1, expected_cause
    ld      t2, 0(t1)
    bne     t0, t2, fail_unexpected_fault
    lla     t1, fault_seen
    sd      t0, 0(t1)
    csrr    t0, mepc
    addi    t0, t0, 4
    csrw    mepc, t0
    mret

fail_access_width:
    li      a0, 1
    j       exit
fail_component_version:
    li      a0, 2
    j       exit
fail_reset_value:
    li      a0, 3
    j       exit
fail_reset_control:
    li      a0, 4
    j       exit
fail_timeout:
    li      a0, 5
    j       exit
fail_masked_status:
    li      a0, 6
    j       exit
fail_masked_raise:
    li      a0, 7
    j       exit
fail_unmask_raise:
    li      a0, 8
    j       exit
fail_claim:
    li      a0, 9
    j       exit
fail_local_status:
    li      a0, 10
    j       exit
fail_eoi_value:
    li      a0, 11
    j       exit
fail_eoi_clear:
    li      a0, 12
    j       exit
fail_complete_lower:
    li      a0, 13
    j       exit
fail_unexpected_fault:
    li      a0, 14

exit:
    lla     a1, semiargs
    sd      a0, 8(a1)
    li      t0, 0x20026         /* ADP_Stopped_ApplicationExit */
    sd      t0, 0(a1)
    li      a0, 0x20            /* TARGET_SYS_EXIT_EXTENDED */

    .balign 16
    slli    zero, zero, 0x1f
    ebreak
    srai    zero, zero, 0x7
    j       .

    .data
    .balign 16
semiargs:
    .space  16
expected_cause:
    .dword  0
fault_seen:
    .dword  0
