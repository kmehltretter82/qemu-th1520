/*
 * Sscofpmf-disabled interrupt CSR WARL test.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ LCOFI, 1 << 13

    .text
    .global _start
_start:
    li      t0, LCOFI
    csrs    mideleg, t0
    csrs    mie, t0
    csrs    mip, t0
    csrs    sie, t0
    csrs    sip, t0

    csrr    t1, mideleg
    and     t1, t1, t0
    bnez    t1, fail_mideleg
    csrr    t1, mie
    and     t1, t1, t0
    bnez    t1, fail_mie
    csrr    t1, mip
    and     t1, t1, t0
    bnez    t1, fail_mip
    csrr    t1, sie
    and     t1, t1, t0
    bnez    t1, fail_sie
    csrr    t1, sip
    and     t1, t1, t0
    bnez    t1, fail_sip
    li      a0, 0
    j       exit

fail_mideleg:
    li      a0, 1
    j       exit
fail_mie:
    li      a0, 2
    j       exit
fail_mip:
    li      a0, 3
    j       exit
fail_sie:
    li      a0, 4
    j       exit
fail_sip:
    li      a0, 5

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
