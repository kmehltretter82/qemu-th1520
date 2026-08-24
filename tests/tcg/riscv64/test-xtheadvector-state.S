/*
 * XTheadVector v0.7.1 state and fixed-point boundary test.
 *
 * The vector instructions are emitted as words so this test does not require
 * an assembler with XTheadVector support.  Encodings come from the frozen
 * XTheadVector specification and target/riscv/xtheadvector.decode.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ MSTATUS_FS_INITIAL,  (1 << 13)
    .equ MSTATUS_VS_THEAD,    (1 << 23)

    .text
    .global _start
_start:
    csrr    t0, mhartid
    bnez    t0, secondary_hart
    lla     t0, trap
    csrw    mtvec, t0
    li      t0, MSTATUS_FS_INITIAL | MSTATUS_VS_THEAD
    csrs    mstatus, t0

    /* Load mask, source and old destination with the full 16-byte VLEN. */
    li      t1, 16
    .word   0x00037057      /* th.vsetvli zero, t1, e8, m1 */
    lla     t2, mask_input
    .word   0x12038007      /* th.vlb.v v0, (t2) */
    lla     t2, add_input
    .word   0x12038087      /* th.vlb.v v1, (t2) */
    lla     t2, old_dest
    .word   0x12038107      /* th.vlb.v v2, (t2) */

    /*
     * Elements before vstart and masked-off body elements stay undisturbed;
     * active elements change and the v0.7.1 tail is zeroed.
     */
    li      t1, 12
    .word   0x00037057      /* th.vsetvli zero, t1, e8, m1 */
    li      t0, 4
    csrw    0x008, t0       /* th.vstart */
    .word   0x0011b157      /* th.vadd.vi v2, v1, 3, v0.t */
    csrr    t0, 0x008
    bnez    t0, fail_1

    li      t1, 16
    .word   0x00037057      /* th.vsetvli zero, t1, e8, m1 */
    lla     t3, masked_output
    .word   0x020e0127      /* th.vsb.v v2, (t3) */
    lla     t0, masked_output
    lla     t1, masked_expected
    li      t2, 16
1:
    lbu     t3, 0(t0)
    lbu     t4, 0(t1)
    bne     t3, t4, fail_2
    addi    t0, t0, 1
    addi    t1, t1, 1
    addi    t2, t2, -1
    bnez    t2, 1b

    /*
     * th.vstart implements only lg2(VLEN) bits.  A value at or beyond vl
     * performs no element or tail writes and still clears th.vstart.
     */
    lla     t2, old_dest
    .word   0x12038107      /* th.vlb.v v2, (t2) */
    li      t1, 12
    .word   0x00037057      /* th.vsetvli zero, t1, e8, m1 */
    li      t0, -1
    csrw    0x008, t0
    csrr    t0, 0x008
    li      t1, 127
    bne     t0, t1, fail_3
    .word   0x0211b157      /* th.vadd.vi v2, v1, 3 */
    csrr    t0, 0x008
    bnez    t0, fail_4

    li      t1, 16
    .word   0x00037057      /* th.vsetvli zero, t1, e8, m1 */
    lla     t3, early_output
    .word   0x020e0127      /* th.vsb.v v2, (t3) */
    lla     t0, early_output
    lla     t1, old_dest
    li      t2, 16
2:
    lbu     t3, 0(t0)
    lbu     t4, 0(t1)
    bne     t3, t4, fail_5
    addi    t0, t0, 1
    addi    t1, t1, 1
    addi    t2, t2, -1
    bnez    t2, 2b

    /* Unsigned saturation sets a sticky vxsat only on overflowing lanes. */
    lla     t2, saturation_input
    .word   0x12038087      /* th.vlb.v v1, (t2) */
    li      t1, 4
    .word   0x00037057      /* th.vsetvli zero, t1, e8, m1 */
    csrw    0x009, zero     /* th.vxsat */
    .word   0x82153157      /* th.vsaddu.vi v2, v1, 10 */
    csrr    t0, 0x009
    li      t1, 1
    bne     t0, t1, fail_6

    li      t1, 16
    .word   0x00037057      /* th.vsetvli zero, t1, e8, m1 */
    lla     t3, saturation_output
    .word   0x020e0127      /* th.vsb.v v2, (t3) */
    lla     t0, saturation_output
    lla     t1, saturation_expected
    li      t2, 16
3:
    lbu     t3, 0(t0)
    lbu     t4, 0(t1)
    bne     t3, t4, fail_8
    addi    t0, t0, 1
    addi    t1, t1, 1
    addi    t2, t2, -1
    bnez    t2, 3b

    li      t1, 4
    .word   0x00037057
    .word   0x82103157      /* A later non-saturating op leaves vxsat set. */
    csrr    t0, 0x009
    li      t1, 1
    bne     t0, t1, fail_7
    csrw    0x009, zero
    csrr    t0, 0x009
    bnez    t0, fail_9

    /* Check all four fixed-point rounding modes with a one-bit shift. */
    li      t1, 16
    .word   0x00037057
    lla     t2, rounding_input
    .word   0x12038087      /* th.vlb.v v1, (t2) */
    li      t1, 8
    .word   0x00037057      /* th.vsetvli zero, t1, e8, m1 */

    csrw    0x00a, zero     /* RNU */
    .word   0xaa10b157      /* th.vssrl.vi v2, v1, 1 */
    li      t1, 16
    .word   0x00037057
    lla     t3, rounding_output
    .word   0x020e0127

    li      t1, 8
    .word   0x00037057
    li      t0, 1           /* RNE */
    csrw    0x00a, t0
    .word   0xaa10b157
    li      t1, 16
    .word   0x00037057
    lla     t3, rounding_output + 16
    .word   0x020e0127

    li      t1, 8
    .word   0x00037057
    li      t0, 2           /* RDN */
    csrw    0x00a, t0
    .word   0xaa10b157
    li      t1, 16
    .word   0x00037057
    lla     t3, rounding_output + 32
    .word   0x020e0127

    li      t1, 8
    .word   0x00037057
    li      t0, -1          /* WARL to ROD */
    csrw    0x00a, t0
    csrr    t1, 0x00a
    li      t2, 3
    bne     t1, t2, fail_10
    .word   0xaa10b157
    li      t1, 16
    .word   0x00037057
    lla     t3, rounding_output + 48
    .word   0x020e0127

    lla     t0, rounding_output
    lla     t1, rounding_expected
    li      t2, 64
4:
    lbu     t3, 0(t0)
    lbu     t4, 0(t1)
    bne     t3, t4, fail_rounding
    addi    t0, t0, 1
    addi    t1, t1, 1
    addi    t2, t2, -1
    bnez    t2, 4b

    li      a0, 0
    j       exit

trap:
    li      a0, 12
    j       exit

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
fail_rounding:
    li      a0, 75          /* 11 + (64 - remaining byte count). */
    sub     a0, a0, t2
    j       exit

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
mask_input:
    .byte   1, 0, 1, 0, 1, 0, 1, 0
    .byte   1, 0, 1, 0, 1, 0, 1, 0
add_input:
    .byte   0, 10, 20, 30, 40, 50, 60, 70
    .byte   80, 90, 100, 110, 120, 130, 140, 150
old_dest:
    .byte   0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7
    .byte   0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf
masked_expected:
    .byte   0xa0, 0xa1, 0xa2, 0xa3, 43, 0xa5, 63, 0xa7
    .byte   83, 0xa9, 103, 0xab, 0, 0, 0, 0
saturation_input:
    .byte   250, 246, 245, 1, 0, 0, 0, 0
    .byte   0, 0, 0, 0, 0, 0, 0, 0
saturation_expected:
    .byte   255, 255, 255, 11, 0, 0, 0, 0
    .byte   0, 0, 0, 0, 0, 0, 0, 0
rounding_input:
    .byte   0, 1, 2, 3, 4, 5, 6, 7
    .byte   0, 0, 0, 0, 0, 0, 0, 0
rounding_expected:
    /* RNU */
    .byte   0, 1, 1, 2, 2, 3, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0
    /* RNE */
    .byte   0, 0, 1, 2, 2, 2, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0
    /* RDN */
    .byte   0, 0, 1, 1, 2, 2, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0
    /* ROD */
    .byte   0, 1, 1, 1, 2, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0

    .data
    .balign 16
masked_output:
    .space  16
early_output:
    .space  16
saturation_output:
    .space  16
rounding_output:
    .space  64
semiargs:
    .space  16
