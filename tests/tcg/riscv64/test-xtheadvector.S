/*
 * C910 XTheadVector architectural smoke test.
 *
 * The instructions are emitted as words so this test does not depend on a
 * toolchain which understands the non-standard, RVV-0.7.1-derived syntax.
 * Encodings come from the frozen XTheadVector specification and are kept in
 * sync with target/riscv/xtheadvector.decode.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ MSTATUS_FS_INITIAL,  (1 << 13)
    .equ MSTATUS_VS_THEAD,    (1 << 23)
    .equ MSTATUS_VS_THEAD_M,  (3 << 23)

    .text
    .global _start
_start:
    csrr    t0, mhartid
    bnez    t0, secondary_hart
    lla     t0, trap
    csrw    mtvec, t0
    li      s11, 0

    /* The identifying tuple is required by the frozen extension spec. */
    csrr    t0, mvendorid
    li      t1, 0x5b7
    bne     t0, t1, fail_1
    csrr    t0, marchid
    bnez    t0, fail_2
    csrr    t0, mimpid
    bnez    t0, fail_2
    csrr    t0, misa
    li      t1, (1 << 21)
    and     t0, t0, t1
    bne     t0, t1, fail_3

    /* Even th.vsetvli must trap while the T-Head VS field is Off. */
    li      t1, 16
    .word   0x000372d7      /* th.vsetvli t0, t1, e8, m1 */
    li      t0, 1
    bne     s11, t0, fail_4

    /* Enable floating-point and XTheadVector state. */
    li      t0, MSTATUS_FS_INITIAL | MSTATUS_VS_THEAD
    csrs    mstatus, t0
    csrr    t1, mstatus
    li      t2, MSTATUS_VS_THEAD_M
    and     t1, t1, t2
    li      t2, MSTATUS_VS_THEAD
    bne     t1, t2, fail_5

    /*
     * A reserved EDIV value makes vtype illegal and vl zero.  Register-form
     * th.vsetvl must not modify its rs2 source while constructing that state.
     */
    li      t1, 16
    li      t2, 0x20
    li      t3, 7
    csrw    0x008, t3       /* th.vstart */
    .word   0x807372d7      /* th.vsetvl t0, t1, t2 */
    bnez    t0, fail_19
    li      t3, 0x20
    bne     t2, t3, fail_20
    csrr    t3, 0xc20       /* th.vl */
    bnez    t3, fail_21
    csrr    t3, 0xc21       /* th.vtype */
    li      t4, 1
    slli    t4, t4, 63
    bne     t3, t4, fail_22
    csrr    t3, 0x008       /* th.vstart */
    bnez    t3, fail_23

    /* Immediate-form illegal vtype follows the same WARL path. */
    li      t3, 5
    csrw    0x008, t3       /* th.vstart */
    .word   0x020372d7      /* th.vsetvli t0, t1, EDIV=1 */
    bnez    t0, fail_24
    csrr    t3, 0xc20       /* th.vl */
    bnez    t3, fail_25
    csrr    t3, 0xc21       /* th.vtype */
    li      t4, 1
    slli    t4, t4, 63
    bne     t3, t4, fail_26
    csrr    t3, 0x008       /* th.vstart */
    bnez    t3, fail_27

    /* Configure 16 8-bit elements in a 128-bit, LMUL=1 register. */
    li      t1, 16
    .word   0x000372d7      /* th.vsetvli t0, t1, e8, m1 */
    li      t1, 16
    bne     t0, t1, fail_6
    csrr    t0, 0xc20       /* th.vl */
    bne     t0, t1, fail_7
    csrr    t0, 0xc21       /* th.vtype */
    bnez    t0, fail_8
    csrr    t0, 0xc22       /* th.vlenb */
    bne     t0, t1, fail_9

    /* The overlapping vcsr CSR does not exist in XTheadVector. */
    csrr    t0, 0x00f
    li      t0, 2
    bne     s11, t0, fail_10

    /* The ratified RVV 1.0-only vsetivli encoding must not execute. */
    .word   0xcc0272d7      /* vsetivli t0, 4, e8, m1, ta, ma */
    li      t0, 3
    bne     s11, t0, fail_11

    /* XTheadZvamo is a separate extension and is not claimed by C910. */
    li      t1, 16
    .word   0x008372d7      /* th.vsetvli t0, t1, e32, m1 */
    lla     t2, input
    .word   0x0213e12f      /* th.vamoaddw.v v2, (t2), v1 */
    li      t0, 4
    bne     s11, t0, fail_12

    /* Restore the byte configuration used below. */
    li      t1, 16
    .word   0x000372d7      /* th.vsetvli t0, t1, e8, m1 */

    /* Exercise vector CSR state independently of the absent vcsr CSR. */
    li      t0, 2
    csrw    0x00a, t0       /* th.vxrm */
    csrr    t1, 0x00a
    bne     t0, t1, fail_13
    li      t0, 1
    csrw    0x009, t0       /* th.vxsat */
    csrr    t1, 0x009
    bne     t0, t1, fail_14

    /* Load bytes, add one to each element, extract one, and store them. */
    lla     t2, input
    lla     t3, output
    .word   0x12038087      /* th.vlb.v v1, (t2) */
    .word   0x0210b157      /* th.vadd.vi v2, v1, 1 */
    .word   0x32202ed7      /* th.vext.x.v t4, v2, x0 */
    li      t0, 1
    bne     t4, t0, fail_15
    .word   0x020e0127      /* th.vsb.v v2, (t3) */

    li      t0, 0
check_output:
    add     t1, t3, t0
    lbu     t2, 0(t1)
    addi    t1, t0, 1
    bne     t2, t1, fail_16
    addi    t0, t0, 1
    li      t1, 16
    blt     t0, t1, check_output

    /* A vector write must expose Dirty through the T-Head status bits. */
    csrr    t0, mstatus
    li      t1, MSTATUS_VS_THEAD_M
    and     t0, t0, t1
    bne     t0, t1, fail_17

    /* Reductions must honor the same VS-Off rule as every vector opcode. */
    li      t0, MSTATUS_VS_THEAD_M
    csrc    mstatus, t0
    .word   0x021121d7      /* th.vredsum.vs v3, v1, v2 */
    li      t0, 5
    bne     s11, t0, fail_28
    li      t0, MSTATUS_VS_THEAD
    csrs    mstatus, t0

    li      a0, 0
    j       exit

trap:
    csrr    t0, mcause
    li      t1, 2
    bne     t0, t1, fail_18
    addi    s11, s11, 1
    csrr    t0, mepc
    addi    t0, t0, 4
    csrw    mepc, t0
    mret

fail_1:  li a0, 1;  j exit
fail_2:  li a0, 2;  j exit
fail_3:  li a0, 3;  j exit
fail_4:  li a0, 4;  j exit
fail_5:  li a0, 5;  j exit
fail_6:  li a0, 6;  j exit
fail_7:  li a0, 7;  j exit
fail_8:  li a0, 8;  j exit
fail_9:  li a0, 9;  j exit
fail_10: li a0, 10; j exit
fail_11: li a0, 11; j exit
fail_12: li a0, 12; j exit
fail_13: li a0, 13; j exit
fail_14: li a0, 14; j exit
fail_15: li a0, 15; j exit
fail_16: li a0, 16; j exit
fail_17: li a0, 17; j exit
fail_18: li a0, 18; j exit
fail_19: li a0, 19; j exit
fail_20: li a0, 20; j exit
fail_21: li a0, 21; j exit
fail_22: li a0, 22; j exit
fail_23: li a0, 23; j exit
fail_24: li a0, 24; j exit
fail_25: li a0, 25; j exit
fail_26: li a0, 26; j exit
fail_27: li a0, 27; j exit
fail_28: li a0, 28; j exit

secondary_hart:
    wfi
    j       secondary_hart

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

    .section .rodata
    .balign 16
input:
    .byte   0, 1, 2, 3, 4, 5, 6, 7
    .byte   8, 9, 10, 11, 12, 13, 14, 15

    .data
    .balign 16
output:
    .space  16
semiargs:
    .space  16
