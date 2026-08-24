/*
 * RISC-V pending-overflow PMU migration guest.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ TEST_STATUS,             TEST_STATUS_ADDR
    .equ TEST_ARM,                TEST_STATUS + 8
    .equ TEST_GO,                 TEST_STATUS + 16
    .equ TEST_STATUS_READY,       1
    .equ TEST_STATUS_ARMED,       2
    .equ TEST_STATUS_PASS,        3
    .equ TEST_FAIL_TRAP,          0xdead3001
    .equ TEST_FAIL_SOURCE_OF,     0xdead3002
    .equ TEST_FAIL_SOURCE_MIP,    0xdead3003
    .equ TEST_FAIL_DEST_OF,       0xdead3004
    .equ TEST_FAIL_DEST_MIP,      0xdead3005
    .equ TEST_FAIL_CLEAR,         0xdead3006
    .equ TEST_FAIL_REARM_OF,      0xdead3007
    .equ TEST_FAIL_REARM_MIP,     0xdead3008
    .equ PMU_COUNTER3_BIT,        0x8
    .equ PMU_INITIAL_VALUE,       -0x100000
#ifdef C910_PMU
    .equ PMU_MIP_BIT,             0x20000
#else
    .equ PMU_MIP_BIT,             0x2000
#endif

    .text
    .global _start
_start:
    csrr    t0, mhartid
    bnez    t0, secondary_hart

    li      s0, TEST_STATUS
    sd      zero, 0(s0)
    sd      zero, TEST_ARM - TEST_STATUS(s0)
    sd      zero, TEST_GO - TEST_STATUS(s0)
    lla     t0, unexpected_trap
    csrw    mtvec, t0

    li      t0, PMU_COUNTER3_BIT
    csrs    mcountinhibit, t0
    li      t1, PMU_EVENT_VALUE
    csrw    mhpmevent3, t1
#ifdef C910_PMU
    csrw    0x7cb, zero            /* th.mcounterof */
    csrw    0x7ca, t0              /* th.mcounterinten */
#else
    li      t1, PMU_MIP_BIT
    csrc    mip, t1
#endif
    li      t1, PMU_INITIAL_VALUE
    csrw    mhpmcounter3, t1

    li      t1, TEST_STATUS_READY
    fence   rw, rw
    sd      t1, 0(s0)

wait_for_arm:
    ld      t1, TEST_ARM - TEST_STATUS(s0)
    beqz    t1, wait_for_arm

    li      t0, PMU_COUNTER3_BIT
    csrc    mcountinhibit, t0
    li      t2, 0x100000
1:
    addi    t2, t2, -1
    bnez    t2, 1b

    /* The first overflow must already be pending on the source. */
#ifdef C910_PMU
    csrr    t1, 0x7cb
    andi    t1, t1, PMU_COUNTER3_BIT
    beqz    t1, fail_source_of
#else
    csrr    t1, mhpmevent3
    bgez    t1, fail_source_of
#endif
    csrr    t1, mip
    li      t2, PMU_MIP_BIT
    and     t1, t1, t2
    beqz    t1, fail_source_mip

    li      t1, TEST_STATUS_ARMED
    fence   rw, rw
    sd      t1, 0(s0)

wait_for_go:
    ld      t1, TEST_GO - TEST_STATUS(s0)
    beqz    t1, wait_for_go

    /* Pending state and its level interrupt must survive migration. */
#ifdef C910_PMU
    csrr    t1, 0x7cb
    andi    t1, t1, PMU_COUNTER3_BIT
    beqz    t1, fail_dest_of
#else
    csrr    t1, mhpmevent3
    bgez    t1, fail_dest_of
#endif
    csrr    t1, mip
    li      t2, PMU_MIP_BIT
    and     t1, t1, t2
    beqz    t1, fail_dest_mip

    /* Clear the pending level, reprogram, and prove a new timer can fire. */
    li      t0, PMU_COUNTER3_BIT
    csrs    mcountinhibit, t0
#ifdef C910_PMU
    csrw    0x7cb, zero
    csrr    t1, 0x7cb
    andi    t1, t1, PMU_COUNTER3_BIT
    bnez    t1, fail_clear
#else
    li      t1, PMU_EVENT_VALUE
    csrw    mhpmevent3, t1
    csrr    t1, mhpmevent3
    bltz    t1, fail_clear
    li      t1, PMU_MIP_BIT
    csrc    mip, t1
#endif
    csrr    t1, mip
    li      t2, PMU_MIP_BIT
    and     t1, t1, t2
    bnez    t1, fail_clear
    li      t1, PMU_INITIAL_VALUE
    csrw    mhpmcounter3, t1
    csrc    mcountinhibit, t0

    li      t2, 0x100000
2:
    addi    t2, t2, -1
    bnez    t2, 2b
#ifdef C910_PMU
    csrr    t1, 0x7cb
    andi    t1, t1, PMU_COUNTER3_BIT
    beqz    t1, fail_rearm_of
#else
    csrr    t1, mhpmevent3
    bgez    t1, fail_rearm_of
#endif
    csrr    t1, mip
    li      t2, PMU_MIP_BIT
    and     t1, t1, t2
    beqz    t1, fail_rearm_mip

    li      t1, TEST_STATUS_PASS
    j       finish

fail_source_of:
    li      t1, TEST_FAIL_SOURCE_OF
    j       finish
fail_source_mip:
    li      t1, TEST_FAIL_SOURCE_MIP
    j       finish
fail_dest_of:
    li      t1, TEST_FAIL_DEST_OF
    j       finish
fail_dest_mip:
    li      t1, TEST_FAIL_DEST_MIP
    j       finish
fail_clear:
    li      t1, TEST_FAIL_CLEAR
    j       finish
fail_rearm_of:
    li      t1, TEST_FAIL_REARM_OF
    j       finish
fail_rearm_mip:
    li      t1, TEST_FAIL_REARM_MIP
    j       finish
unexpected_trap:
    li      t1, TEST_FAIL_TRAP

finish:
    fence   rw, rw
    sd      t1, 0(s0)

secondary_hart:
    wfi
    j       secondary_hart
