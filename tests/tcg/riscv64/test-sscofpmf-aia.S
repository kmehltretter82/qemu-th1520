/*
 * Sscofpmf AIA supervisor and VS virtual-interrupt CSR test.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ LCOFI,      1 << 13
    .equ CSR_MVIEN,  0x308
    .equ CSR_MVIP,   0x309
    .equ CSR_VSIE,   0x204
    .equ CSR_VSIP,   0x244
    .equ CSR_HVIEN,  0x608
    .equ CSR_HVIP,   0x645

#if defined(SSCOFPMF_DISABLED) || defined(SMCDELEG_DISABLED)
#define MVIRTUAL_LCOFI_DISABLED
#endif
#if defined(MVIRTUAL_LCOFI_DISABLED) || defined(SSCCFG_DISABLED)
#define HVIRTUAL_LCOFI_DISABLED
#endif

    .text
    .global _start
_start:
    li      t0, LCOFI
    csrc    mideleg, t0
    csrs    CSR_MVIEN, t0
    csrs    CSR_MVIP, t0
    csrs    CSR_HVIEN, t0
    csrs    CSR_HVIP, t0

#ifdef MVIRTUAL_LCOFI_DISABLED
    csrr    t1, CSR_MVIEN
    and     t1, t1, t0
    bnez    t1, fail_mvien
    csrr    t1, CSR_MVIP
    and     t1, t1, t0
    bnez    t1, fail_mvip
#else
    csrr    t1, CSR_MVIEN
    and     t1, t1, t0
    beqz    t1, fail_mvien
    csrr    t1, CSR_MVIP
    and     t1, t1, t0
    beqz    t1, fail_mvip
    csrs    sie, t0
    csrr    t1, sie
    and     t1, t1, t0
    beqz    t1, fail_sie
    csrr    t1, sip
    and     t1, t1, t0
    beqz    t1, fail_sip
    csrc    sip, t0
    csrr    t1, CSR_MVIP
    and     t1, t1, t0
    bnez    t1, fail_sip_ack
#endif

#ifdef HVIRTUAL_LCOFI_DISABLED
    csrs    CSR_VSIE, t0
    csrs    CSR_VSIP, t0
    csrr    t1, CSR_HVIEN
    and     t1, t1, t0
    bnez    t1, fail_hvien
    csrr    t1, CSR_HVIP
    and     t1, t1, t0
    bnez    t1, fail_hvip
    csrr    t1, CSR_VSIE
    and     t1, t1, t0
    bnez    t1, fail_vsie
    csrr    t1, CSR_VSIP
    and     t1, t1, t0
    bnez    t1, fail_vsip
#else
    csrr    t1, CSR_HVIEN
    and     t1, t1, t0
    beqz    t1, fail_hvien
    csrr    t1, CSR_HVIP
    and     t1, t1, t0
    beqz    t1, fail_hvip
    csrs    CSR_VSIE, t0
    csrr    t1, CSR_VSIE
    and     t1, t1, t0
    beqz    t1, fail_vsie
    csrr    t1, CSR_VSIP
    and     t1, t1, t0
    beqz    t1, fail_vsip
    csrc    CSR_VSIP, t0
    csrr    t1, CSR_HVIP
    and     t1, t1, t0
    bnez    t1, fail_vsip_ack
#endif

    li      a0, 0
    j       exit

fail_mvien:
    li      a0, 1
    j       exit
fail_mvip:
    li      a0, 2
    j       exit
fail_sie:
    li      a0, 3
    j       exit
fail_sip:
    li      a0, 4
    j       exit
fail_sip_ack:
    li      a0, 5
    j       exit
fail_hvien:
    li      a0, 6
    j       exit
fail_hvip:
    li      a0, 7
    j       exit
fail_vsie:
    li      a0, 8
    j       exit
fail_vsip:
    li      a0, 9
    j       exit
fail_vsip_ack:
    li      a0, 10

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
