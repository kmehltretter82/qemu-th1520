/*
 * DesignWare APB UART access and end-to-end interrupt delivery test.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ DW_UART_BASE,          0xffe7014000
    .equ DW_UART_RBR_THR_DLL,   0x00
    .equ DW_UART_IER_DLH,       0x04
    .equ DW_UART_IIR_FCR,       0x08
    .equ DW_UART_LCR,           0x0c
    .equ DW_UART_LSR,           0x14
    .equ DW_UART_SCR,           0x1c
    .equ DW_UART_USR,           0x7c
    .equ DW_UART_SRR,           0x88
    .equ DW_UART_CTR,           0xfc

    .equ C900_PLIC_BASE,        0xffd8000000
    .equ PLIC_UART_PRIORITY,    (C900_PLIC_BASE + 0x90)
    .equ PLIC_M_ENABLE_1,       (C900_PLIC_BASE + 0x2004)
    .equ PLIC_M_CLAIM,          (C900_PLIC_BASE + 0x200004)

    .equ UART_IRQ,              36
    .equ UART_IRQ_WORD_BIT,     (1 << 4)
    .equ UART_IER_THRI,         (1 << 1)
    .equ UART_IIR_THRI,         0x02
    .equ UART_IIR_BUSY,         0x07
    .equ UART_IIR_NO_INT,       0x01
    .equ UART_LCR_DLAB,         (1 << 7)
    .equ UART_LSR_THRE_TEMT,    ((1 << 5) | (1 << 6))
    .equ UART_USR_BUSY,         (1 << 0)
    .equ MIP_MEIP,              (1 << 11)

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
    li      s0, DW_UART_BASE

    /* The TH1520 integration rejects subword APB transactions. */
    check_load_fault lb
    check_load_fault lh
    check_store_fault sb
    check_store_fault sh

    /*
     * QEMU's sub-page system-bus mapping splits an aligned RV64 transaction
     * into two APB words.  UART-001 tracks whether TH1520 does the same.
     */
    lla     t0, expected_cause
    sd      zero, 0(t0)
    li      t0, UART_LCR_DLAB | 3
    sw      t0, DW_UART_LCR(s0)
    li      t0, 0x0000005600000078
    sd      t0, DW_UART_RBR_THR_DLL(s0)
    ld      t1, DW_UART_RBR_THR_DLL(s0)
    bne     t0, t1, fail_wide_split
    li      t0, 1
    sw      t0, DW_UART_SRR(s0)

    /* Aligned 32-bit accesses reach the UART and preserve byte registers. */
    lw      t0, DW_UART_CTR(s0)
    li      t1, 0x44570110
    bne     t0, t1, fail_component_type
    li      t0, 0x123456a5
    sw      t0, DW_UART_SCR(s0)
    lw      t0, DW_UART_SCR(s0)
    li      t1, 0xa5
    bne     t0, t1, fail_scratch

    /* Route the UART's level interrupt to hart 0's machine context. */
    li      t0, PLIC_UART_PRIORITY
    li      t1, 5
    sw      t1, 0(t0)
    li      t0, PLIC_M_ENABLE_1
    li      t1, UART_IRQ_WORD_BIT
    sw      t1, 0(t0)

    /* Enabling THRE must propagate UART -> PLIC source 36 -> MEIP. */
    li      t0, UART_IER_THRI
    sw      t0, DW_UART_IER_DLH(s0)
    fence   iorw, iorw
    csrr    t0, mip
    li      t1, MIP_MEIP
    and     t0, t0, t1
    beqz    t0, fail_thre_raise
    li      t0, PLIC_M_CLAIM
    lw      t1, 0(t0)
    li      t2, UART_IRQ
    bne     t1, t2, fail_thre_claim
    lw      t1, DW_UART_IIR_FCR(s0)
    andi    t1, t1, 0xf
    li      t3, UART_IIR_THRI
    bne     t1, t3, fail_thre_iir
    sw      t2, 0(t0)
    fence   iorw, iorw
    csrr    t1, mip
    li      t2, MIP_MEIP
    and     t1, t1, t2
    bnez    t1, fail_thre_lower

    /*
     * A long divisor makes the busy window deterministic.  A forbidden LCR
     * write is ignored and raises the DesignWare busy-detect interrupt.
     */
    sw      zero, DW_UART_IER_DLH(s0)
    li      t0, UART_LCR_DLAB | 3
    sw      t0, DW_UART_LCR(s0)
    li      t0, 0xff
    sw      t0, DW_UART_RBR_THR_DLL(s0)
    sw      t0, DW_UART_IER_DLH(s0)
    li      t0, 3
    sw      t0, DW_UART_LCR(s0)
    li      t0, 'x'
    sw      t0, DW_UART_RBR_THR_DLL(s0)
    li      t0, UART_LCR_DLAB | 3
    sw      t0, DW_UART_LCR(s0)
    lw      t0, DW_UART_LCR(s0)
    li      t1, 3
    bne     t0, t1, fail_busy_lcr
    lw      t0, DW_UART_IIR_FCR(s0)
    andi    t0, t0, 0xf
    li      t1, UART_IIR_BUSY
    bne     t0, t1, fail_busy_iir
    fence   iorw, iorw
    csrr    t0, mip
    li      t1, MIP_MEIP
    and     t0, t0, t1
    beqz    t0, fail_busy_raise
    li      t0, PLIC_M_CLAIM
    lw      t1, 0(t0)
    li      t2, UART_IRQ
    bne     t1, t2, fail_busy_claim
    lw      t1, DW_UART_USR(s0)
    andi    t1, t1, UART_USR_BUSY
    beqz    t1, fail_busy_usr
    lw      t1, DW_UART_IIR_FCR(s0)
    andi    t1, t1, 0xf
    li      t3, UART_IIR_NO_INT
    bne     t1, t3, fail_busy_clear
    sw      t2, 0(t0)
    fence   iorw, iorw
    csrr    t1, mip
    li      t2, MIP_MEIP
    and     t1, t1, t2
    bnez    t1, fail_busy_lower

    /* A DesignWare UART reset resets both wrapper and 16550 state. */
    li      t0, 0x5a
    sw      t0, DW_UART_SCR(s0)
    li      t0, 1
    sw      t0, DW_UART_SRR(s0)
    lw      t0, DW_UART_SCR(s0)
    bnez    t0, fail_uart_reset
    lw      t0, DW_UART_LSR(s0)
    li      t1, UART_LSR_THRE_TEMT
    bne     t0, t1, fail_uart_reset

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
fail_component_type:
    li      a0, 2
    j       exit
fail_scratch:
    li      a0, 3
    j       exit
fail_thre_raise:
    li      a0, 4
    j       exit
fail_thre_claim:
    li      a0, 5
    j       exit
fail_thre_iir:
    li      a0, 6
    j       exit
fail_thre_lower:
    li      a0, 7
    j       exit
fail_busy_lcr:
    li      a0, 8
    j       exit
fail_busy_iir:
    li      a0, 9
    j       exit
fail_busy_raise:
    li      a0, 10
    j       exit
fail_busy_claim:
    li      a0, 11
    j       exit
fail_busy_usr:
    li      a0, 12
    j       exit
fail_busy_clear:
    li      a0, 13
    j       exit
fail_busy_lower:
    li      a0, 14
    j       exit
fail_uart_reset:
    li      a0, 15
    j       exit
fail_unexpected_fault:
    li      a0, 16
    j       exit
fail_wide_split:
    li      a0, 17

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
expected_cause:
    .dword  0
fault_seen:
    .dword  0
