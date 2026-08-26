/*
 * XTheadVector scalar- and vector-index permutation boundary test.
 *
 * Vector instructions are emitted as words so the test does not require an
 * assembler with XTheadVector support.  Independent scalar RV64 loops compute
 * the expected values using full-width, overflow-safe index arithmetic.  Trap
 * recovery checks the instruction-specific masked overlap rules.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ MSTATUS_VS_THEAD, (1 << 23)

    .macro SET_E8_M1
    .word   0x00037057      /* th.vsetvli zero, t1, e8, m1 */
    .endm

    .macro LOAD_VECTORS mask
    li      t1, 16
    SET_E8_M1
    lla     t2, source
    .word   0x12038087      /* th.vlb.v v1, (t2) */
    lla     t2, old_dest
    .word   0x12038107      /* th.vlb.v v2, (t2) */
    lla     t2, \mask
    .word   0x12038007      /* th.vlb.v v0, (t2) */
    .endm

    .macro STORE_V2
    li      t1, 16
    SET_E8_M1
    lla     t3, output
    .word   0x020e0127      /* th.vsb.v v2, (t3) */
    .endm

    .macro EXPECT_ILLEGAL encoding, count, failure
    li      s0, 1
    .word   \encoding
    li      s0, 0
    li      t0, \count
    beq     s1, t0, 1f
    li      a0, \failure
    j       exit
1:
    .endm

    .text
    .global _start
_start:
    csrr    t0, mhartid
    bnez    t0, secondary_hart
    lla     t0, trap
    csrw    mtvec, t0
    li      t0, MSTATUS_VS_THEAD
    csrs    mstatus, t0
    li      s0, 0             /* Any trap is unexpected until enabled below. */
    li      s1, 0             /* Count accepted illegal-instruction traps. */

    /* Valid slide control: vd[i] = vs2[i + 2], followed by zero tail. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 2
    li      a5, 8
    li      a6, 0
    li      a7, 0
    call    oracle_slidedown
    LOAD_VECTORS mask_all
    li      t1, 8
    SET_E8_M1
    li      t0, 2
    .word   0x3e12c157      /* th.vslidedown.vx v2, v1, t0 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 1
    call    check_16

    /* VLMAX itself is out of range for every active destination lane. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 16
    li      a5, 8
    li      a6, 0
    li      a7, 0
    call    oracle_slidedown
    LOAD_VECTORS mask_all
    li      t1, 8
    SET_E8_M1
    li      t0, 16
    .word   0x3e12c157      /* th.vslidedown.vx v2, v1, t0 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 2
    call    check_16

    /* XLEN-wide -1 must not wrap i + offset back into the source group. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, -1
    li      a5, 4
    li      a6, 0
    li      a7, 0
    call    oracle_slidedown
    LOAD_VECTORS mask_all
    li      t1, 4
    SET_E8_M1
    li      t0, -1
    .word   0x3e12c157      /* th.vslidedown.vx v2, v1, t0 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 3
    call    check_16

    /* Masked-off and prestart bytes stay old; the v0.7.1 tail is zero. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_alt
    li      a4, 1
    li      a5, 8
    li      a6, 2
    li      a7, 1
    call    oracle_slidedown
    LOAD_VECTORS mask_alt
    li      t1, 8
    SET_E8_M1
    li      t0, 2
    csrw    0x008, t0       /* th.vstart */
    li      t0, 1
    .word   0x3c12c157      /* th.vslidedown.vx v2, v1, t0, v0.t */
    csrr    t0, 0x008
    beqz    t0, 1f
    li      a0, 4
    j       exit
1:
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 5
    call    check_16

    /* In-place slidedown is safe because higher source lanes are read first. */
    lla     a0, expected
    lla     a1, source
    lla     a2, source
    lla     a3, mask_all
    li      a4, 2
    li      a5, 8
    li      a6, 0
    li      a7, 0
    call    oracle_slidedown
    LOAD_VECTORS mask_all
    li      t1, 8
    SET_E8_M1
    li      t0, 2
    .word   0x3e12c0d7      /* th.vslidedown.vx v1, v1, t0 */
    li      t1, 16
    SET_E8_M1
    lla     t3, output
    .word   0x020e00a7      /* th.vsb.v v1, (t3) */
    lla     a0, output
    lla     a1, expected
    li      a2, 6
    call    check_16

    /* A valid offset may cross VLMAX partway through the active body. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 12
    li      a5, 8
    li      a6, 0
    li      a7, 0
    call    oracle_slidedown
    LOAD_VECTORS mask_all
    li      t1, 8
    SET_E8_M1
    li      t0, 12
    .word   0x3e12c157      /* th.vslidedown.vx v2, v1, t0 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 13
    call    check_16

    /* Masking and vstart force the gather helper with a valid scalar index. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_alt
    li      a4, 3
    li      a5, 8
    li      a6, 1
    li      a7, 1
    call    oracle_gather
    LOAD_VECTORS mask_alt
    li      t1, 8
    SET_E8_M1
    li      t0, 1
    csrw    0x008, t0       /* th.vstart */
    li      t0, 3
    .word   0x3012c157      /* th.vrgather.vx v2, v1, t0, v0.t */
    csrr    t0, 0x008
    beqz    t0, 2f
    li      a0, 7
    j       exit
2:
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 8
    call    check_16

    /* The fallback helper must retain all 64 index bits. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 1
    slli    a4, a4, 32
    li      a5, 8
    li      a6, 0
    li      a7, 1
    call    oracle_gather
    LOAD_VECTORS mask_all
    li      t1, 8
    SET_E8_M1
    li      t0, 1
    slli    t0, t0, 32
    .word   0x3012c157      /* th.vrgather.vx v2, v1, t0, v0.t */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 9
    call    check_16

    /* An unmasked short-vl operation also uses the helper and keeps -1 wide. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, -1
    li      a5, 8
    li      a6, 0
    li      a7, 0
    call    oracle_gather
    LOAD_VECTORS mask_all
    li      t1, 8
    SET_E8_M1
    li      t0, -1
    .word   0x3212c157      /* th.vrgather.vx v2, v1, t0 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 10
    call    check_16

    /* Full-vl, unmasked gather takes the optimized scalar-load path. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 5
    li      a5, 16
    li      a6, 0
    li      a7, 0
    call    oracle_gather
    LOAD_VECTORS mask_all
    li      t1, 16
    SET_E8_M1
    li      t0, 5
    .word   0x3212c157      /* th.vrgather.vx v2, v1, t0 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 11
    call    check_16

    /* The optimized path must agree on the same 64-bit out-of-range index. */
    lla     a0, expected
    lla     a1, old_dest
    lla     a2, source
    lla     a3, mask_all
    li      a4, 1
    slli    a4, a4, 32
    li      a5, 16
    li      a6, 0
    li      a7, 0
    call    oracle_gather
    LOAD_VECTORS mask_all
    li      t1, 16
    SET_E8_M1
    li      t0, 1
    slli    t0, t0, 32
    .word   0x3212c157      /* th.vrgather.vx v2, v1, t0 */
    STORE_V2
    lla     a0, output
    lla     a1, expected
    li      a2, 12
    call    check_16

    /* Every e64 vector index bit participates in the vrgather.vv bound. */
    lla     a0, expected
    lla     a1, gather64_source
    lla     a2, gather64_index
    call    oracle_gather_vv_e64
    li      t1, 2
    .word   0x00c37057      /* th.vsetvli zero, t1, e64, m1 */
    lla     t2, gather64_source
    .word   0x0203f087      /* th.vle.v v1, (t2) */
    lla     t2, gather64_index
    .word   0x0203f107      /* th.vle.v v2, (t2) */
    .word   0x321101d7      /* th.vrgather.vv v3, v1, v2 */
    lla     t2, output
    .word   0x0203f1a7      /* th.vse.v v3, (t2) */
    lla     a0, output
    lla     a1, expected
    li      a2, 14
    call    check_16

    /*
     * RVV 0.7.1 forbids masked slide-down destinations from overlapping v0,
     * including at LMUL=1 where its generic overlap rule would allow it.
    */
    LOAD_VECTORS mask_alt
    li      t0, 1
    csrw    0x008, t0       /* The first illegal trap must preserve vstart. */
    EXPECT_ILLEGAL 0x3c12c057, 1, 15 /* th.vslidedown.vx v0, v1, t0, v0.t */

    csrr    t0, 0x008
    li      t1, 1
    beq     t0, t1, 3f
    li      a0, 16
    j       exit
3:
    csrw    0x008, zero
    li      t1, 16
    SET_E8_M1
    lla     t3, trap_v0_output
    .word   0x020e0027      /* th.vsb.v v0, (t3) */
    lla     a0, trap_v0_output
    lla     a1, mask_alt
    li      a2, 17
    call    check_16

    EXPECT_ILLEGAL 0x3c10b057, 2, 18 /* th.vslidedown.vi v0, v1, 1, v0.t */
    EXPECT_ILLEGAL 0x3c12e057, 3, 19 /* th.vslide1down.vx v0, v1, t0, v0.t */

    /* Unmasked v0 destinations and masked non-v0 destinations remain legal. */
    li      t0, 1
    .word   0x3e12c057      /* th.vslidedown.vx v0, v1, t0 */
    .word   0x3e10b057      /* th.vslidedown.vi v0, v1, 1 */
    .word   0x3e12e057      /* th.vslide1down.vx v0, v1, t0 */
    .word   0x3c12c157      /* th.vslidedown.vx v2, v1, t0, v0.t */
    .word   0x3c10b157      /* th.vslidedown.vi v2, v1, 1, v0.t */
    .word   0x3c12e157      /* th.vslide1down.vx v2, v1, t0, v0.t */

    li      t0, 3
    beq     s1, t0, 4f
    li      a0, 20
    j       exit
4:

    li      a0, 0
    j       exit

/*
 * a0 expected, a1 old destination, a2 source, a3 mask, a4 offset,
 * a5 vl, a6 vstart, a7 masked.  VLMAX is 16 for e8,m1.
 */
oracle_slidedown:
    li      t0, 0
.Lslide_copy:
    add     t1, a1, t0
    lbu     t2, 0(t1)
    add     t1, a0, t0
    sb      t2, 0(t1)
    addi    t0, t0, 1
    li      t1, 16
    bltu    t0, t1, .Lslide_copy

    mv      t0, a6
.Lslide_body:
    bgeu    t0, a5, .Lslide_tail
    beqz    a7, .Lslide_active
    add     t1, a3, t0
    lbu     t2, 0(t1)
    andi    t2, t2, 1
    beqz    t2, .Lslide_next
.Lslide_active:
    li      t3, 16
    bgeu    a4, t3, .Lslide_zero
    sub     t3, t3, a4
    bgeu    t0, t3, .Lslide_zero
    add     t4, a2, a4
    add     t4, t4, t0
    lbu     t5, 0(t4)
    j       .Lslide_store
.Lslide_zero:
    li      t5, 0
.Lslide_store:
    add     t4, a0, t0
    sb      t5, 0(t4)
.Lslide_next:
    addi    t0, t0, 1
    j       .Lslide_body

.Lslide_tail:
    mv      t0, a5
.Lslide_tail_loop:
    li      t1, 16
    bgeu    t0, t1, .Lslide_done
    add     t1, a0, t0
    sb      zero, 0(t1)
    addi    t0, t0, 1
    j       .Lslide_tail_loop
.Lslide_done:
    ret

/*
 * a0 expected, a1 old destination, a2 source, a3 mask, a4 index,
 * a5 vl, a6 vstart, a7 masked.  VLMAX is 16 for e8,m1.
 */
oracle_gather:
    li      t0, 0
.Lgather_copy:
    add     t1, a1, t0
    lbu     t2, 0(t1)
    add     t1, a0, t0
    sb      t2, 0(t1)
    addi    t0, t0, 1
    li      t1, 16
    bltu    t0, t1, .Lgather_copy

    li      t6, 16
    bgeu    a4, t6, .Lgather_value_zero
    add     t6, a2, a4
    lbu     t6, 0(t6)
    j       .Lgather_value_ready
.Lgather_value_zero:
    li      t6, 0
.Lgather_value_ready:
    mv      t0, a6
.Lgather_body:
    bgeu    t0, a5, .Lgather_tail
    beqz    a7, .Lgather_active
    add     t1, a3, t0
    lbu     t2, 0(t1)
    andi    t2, t2, 1
    beqz    t2, .Lgather_next
.Lgather_active:
    add     t1, a0, t0
    sb      t6, 0(t1)
.Lgather_next:
    addi    t0, t0, 1
    j       .Lgather_body

.Lgather_tail:
    mv      t0, a5
.Lgather_tail_loop:
    li      t1, 16
    bgeu    t0, t1, .Lgather_done
    add     t1, a0, t0
    sb      zero, 0(t1)
    addi    t0, t0, 1
    j       .Lgather_tail_loop
.Lgather_done:
    ret

/* a0 expected, a1 source, a2 indices.  VLMAX is 2 for e64,m1. */
oracle_gather_vv_e64:
    li      t0, 0
    li      t6, 2
.Lgather_vv_e64_loop:
    slli    t1, t0, 3
    add     t2, a2, t1
    ld      t3, 0(t2)
    bgeu    t3, t6, .Lgather_vv_e64_zero
    slli    t3, t3, 3
    add     t4, a1, t3
    ld      t5, 0(t4)
    j       .Lgather_vv_e64_store
.Lgather_vv_e64_zero:
    li      t5, 0
.Lgather_vv_e64_store:
    add     t4, a0, t1
    sd      t5, 0(t4)
    addi    t0, t0, 1
    bltu    t0, t6, .Lgather_vv_e64_loop
    ret

check_16:
    li      t0, 16
.Lcheck_loop:
    lbu     t1, 0(a0)
    lbu     t2, 0(a1)
    bne     t1, t2, .Lcheck_failed
    addi    a0, a0, 1
    addi    a1, a1, 1
    addi    t0, t0, -1
    bnez    t0, .Lcheck_loop
    ret
.Lcheck_failed:
    mv      a0, a2
    j       exit

trap:
    csrr    t0, mcause
    li      t1, 2
    bne     t0, t1, .Lbad_trap_cause
    li      t0, 1
    bne     s0, t0, .Lunexpected_trap
    addi    s1, s1, 1
    csrr    t0, mepc
    addi    t0, t0, 4
    csrw    mepc, t0
    mret
.Lbad_trap_cause:
    li      a0, 90
    j       exit
.Lunexpected_trap:
    li      a0, 91
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
source:
    .byte   0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
    .byte   0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
old_dest:
    .byte   0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7
    .byte   0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf
mask_all:
    .byte   1, 1, 1, 1, 1, 1, 1, 1
    .byte   1, 1, 1, 1, 1, 1, 1, 1
mask_alt:
    .byte   1, 0, 1, 0, 1, 0, 1, 0
    .byte   1, 0, 1, 0, 1, 0, 1, 0
    .balign 16
gather64_source:
    .dword  0x1122334455667788, 0x99aabbccddeeff00
gather64_index:
    .dword  0x0000000100000000, 1

    .data
    .balign 16
expected:
    .space  16
output:
    .space  16
trap_v0_output:
    .space  16
semiargs:
    .space  16
