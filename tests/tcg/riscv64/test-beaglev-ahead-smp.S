/*
 * BeagleV Ahead four-hart reset and UART serialization test.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ DW_UART_BASE,          0xffe7014000
    .equ DW_UART_RBR_THR_DLL,   0x00
    .equ DW_UART_LCR,           0x0c
    .equ DW_UART_LSR,           0x14
    .equ DW_UART_SRR,           0x88

    .equ UART_LSR_THRE,         (1 << 5)
    .equ UART_LSR_TEMT,         (1 << 6)

    .text
    .global _start
_start:
    csrr    s0, mhartid
    li      t0, 4
    bgeu    s0, t0, fail_bad_hart

    /* Record that every board-model hart reached the shared payload. */
    lla     t0, arrived_mask
    li      t1, 1
    sll     t1, t1, s0
    amoor.w.aqrl zero, t1, (t0)

    bnez    s0, wait_ready

    /* Hart 0 owns initialization; all other harts wait for its release. */
    li      s1, DW_UART_BASE
    li      t0, 1
    sw      t0, DW_UART_SRR(s1)
    li      t0, 3
    sw      t0, DW_UART_LCR(s1)
    lla     t0, ready
    li      t1, 1
    amoswap.w.rl zero, t1, (t0)

wait_ready:
    lla     t0, ready
1:
    amoadd.w.aq t1, zero, (t0)
    beqz    t1, 1b
    li      s1, DW_UART_BASE

    /* Serialize UART ownership in hart-ID order. */
    lla     t0, turn
2:
    amoadd.w.aq t1, zero, (t0)
    bne     t1, s0, 2b

3:
    lw      t1, DW_UART_LSR(s1)
    andi    t1, t1, UART_LSR_THRE
    beqz    t1, 3b
    addi    t1, s0, '0'
    sw      t1, DW_UART_RBR_THR_DLL(s1)

    li      t0, 3
    bne     s0, t0, mark_done
    li      t1, '\n'
    sw      t1, DW_UART_RBR_THR_DLL(s1)

mark_done:
    lla     t0, done_mask
    li      t1, 1
    sll     t1, t1, s0
    amoor.w.aqrl zero, t1, (t0)
    lla     t0, turn
    addi    t1, s0, 1
    amoswap.w.rl zero, t1, (t0)

    bnez    s0, secondary_done

    /* Hart 0 cannot report success until all four UART writes completed. */
4:
    amoadd.w.aq t1, zero, (t0)
    li      t2, 4
    bne     t1, t2, 4b
    lla     t0, arrived_mask
    amoadd.w.aq t1, zero, (t0)
    li      t2, 0xf
    bne     t1, t2, fail_arrived
    lla     t0, done_mask
    amoadd.w.aq t1, zero, (t0)
    bne     t1, t2, fail_done

    /* Ensure the final byte has left the modeled transmit shift register. */
5:
    lw      t1, DW_UART_LSR(s1)
    andi    t1, t1, UART_LSR_TEMT
    beqz    t1, 5b

    li      a0, 0
    j       exit

secondary_done:
    wfi
    j       secondary_done

fail_bad_hart:
    li      a0, 1
    j       exit
fail_arrived:
    li      a0, 2
    j       exit
fail_done:
    li      a0, 3

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
ready:
    .word   0
turn:
    .word   0
arrived_mask:
    .word   0
done_mask:
    .word   0
semiargs:
    .space  16
