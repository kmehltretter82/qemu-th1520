/*
 * RISC-V PMU migration guest.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ TEST_STATUS,             TEST_STATUS_ADDR
    .equ TEST_ARM,                TEST_STATUS + 8
    .equ TEST_GO,                 TEST_STATUS + 16
    .equ TEST_SOURCE_COUNT,       TEST_STATUS + 24
    .equ TEST_DEST_COUNT,         TEST_STATUS + 32
    .equ TEST_SOURCE_CYCLE,       TEST_STATUS + 40
    .equ TEST_DEST_CYCLE,         TEST_STATUS + 48
    .equ TEST_STATUS_READY,       1
    .equ TEST_STATUS_ARMED,       2
    .equ TEST_STATUS_PASS,        3
    .equ TEST_FAIL_COUNTER,       0xdead0001
    .equ TEST_FAIL_OVERFLOW,      0xdead0002
    .equ TEST_FAIL_TRAP,          0xdead0003
    .equ TEST_FAIL_CONTINUITY,    0xdead0004
    .equ TEST_FAIL_CONFIG,        0xdead0005
    .equ TEST_FAIL_FIXED_CONT,    0xdead0006
    .equ TEST_FAIL_FIXED_COUNT,   0xdead0007
    .equ PMU_COUNTER3_BIT,        0x8
    .equ PMU_EVENT_INSTRUCTIONS,  PMU_EVENT_VALUE
    .equ PMU_INITIAL_VALUE,       -0x1000000

    .text
    .global _start
_start:
    csrr    t0, mhartid
    bnez    t0, secondary_hart

    li      s0, TEST_STATUS
    sd      zero, 0(s0)
    sd      zero, TEST_ARM - TEST_STATUS(s0)
    sd      zero, TEST_GO - TEST_STATUS(s0)
    sd      zero, TEST_SOURCE_COUNT - TEST_STATUS(s0)
    sd      zero, TEST_DEST_COUNT - TEST_STATUS(s0)
    sd      zero, TEST_SOURCE_CYCLE - TEST_STATUS(s0)
    sd      zero, TEST_DEST_CYCLE - TEST_STATUS(s0)
    lla     t0, unexpected_trap
    csrw    mtvec, t0

    /* Configure counter 3 before migration, but keep it inhibited. */
    li      t0, PMU_COUNTER3_BIT
    csrs    mcountinhibit, t0
    li      t1, PMU_EVENT_INSTRUCTIONS
    csrw    mhpmevent3, t1
    li      t1, PMU_INITIAL_VALUE
    csrw    mhpmcounter3, t1
#ifdef C910_PMU
    csrw    0x7cb, zero
#else
    /* Keep M-mode counting enabled while testing Smcntrpmf migration. */
    li      t1, 1
    slli    t1, t1, 60
    csrw    0x321, t1
    csrw    0x322, t1
#endif

    li      t1, TEST_STATUS_READY
    fence   rw, rw
    sd      t1, 0(s0)

wait_for_arm:
    ld      t1, TEST_ARM - TEST_STATUS(s0)
    beqz    t1, wait_for_arm

    /* Arm the source timer; the host stops us as soon as this is visible. */
    li      t0, PMU_COUNTER3_BIT
    csrc    mcountinhibit, t0

    /* Leave a measurable source-side delta for the migration check. */
    li      t2, 0x1000
1:
    addi    t2, t2, -1
    bnez    t2, 1b
    csrr    t1, mhpmcounter3
    sd      t1, TEST_SOURCE_COUNT - TEST_STATUS(s0)
    csrr    t1, mcycle
    sd      t1, TEST_SOURCE_CYCLE - TEST_STATUS(s0)

    li      t1, TEST_STATUS_ARMED
    fence   rw, rw
    sd      t1, 0(s0)

wait_for_go:
    ld      t1, TEST_GO - TEST_STATUS(s0)
    beqz    t1, wait_for_go

#ifndef C910_PMU
    li      t2, 1
    slli    t2, t2, 60
    csrr    t1, 0x321
    bne     t1, t2, fail_config
    csrr    t1, 0x322
    bne     t1, t2, fail_config
#endif

    csrr    t1, mcycle
    ld      t2, TEST_SOURCE_CYCLE - TEST_STATUS(s0)
    bltu    t1, t2, fail_fixed_continuity
    sd      t1, TEST_DEST_CYCLE - TEST_STATUS(s0)

    /* The destination must continue from the materialized source value. */
    csrr    t1, mhpmcounter3
    ld      t2, TEST_SOURCE_COUNT - TEST_STATUS(s0)
    bltu    t1, t2, fail_continuity
    sd      t1, TEST_DEST_COUNT - TEST_STATUS(s0)

    /* Cross the wrap point without reprogramming the migrated timer. */
    li      t2, 0x1000000
2:
    addi    t2, t2, -1
    bnez    t2, 2b

    li      t0, PMU_COUNTER3_BIT | 1
    csrs    mcountinhibit, t0
    csrr    t1, mhpmcounter3
    ld      t2, TEST_DEST_COUNT - TEST_STATUS(s0)
    beq     t1, t2, fail_counter
    csrr    t1, mcycle
    ld      t2, TEST_DEST_CYCLE - TEST_STATUS(s0)
    beq     t1, t2, fail_fixed_counter
#ifdef C910_PMU
    csrr    t1, 0x7cb
    andi    t1, t1, PMU_COUNTER3_BIT
    beqz    t1, fail_overflow
#else
    csrr    t1, mhpmevent3
    bgez    t1, fail_overflow
#endif

    li      t1, TEST_STATUS_PASS
    j       finish

fail_counter:
    li      t1, TEST_FAIL_COUNTER
    j       finish
fail_overflow:
    li      t1, TEST_FAIL_OVERFLOW
    j       finish
fail_continuity:
    li      t1, TEST_FAIL_CONTINUITY
    j       finish
fail_config:
    li      t1, TEST_FAIL_CONFIG
    j       finish
fail_fixed_continuity:
    li      t1, TEST_FAIL_FIXED_CONT
    j       finish
fail_fixed_counter:
    li      t1, TEST_FAIL_FIXED_COUNT
    j       finish
unexpected_trap:
    li      t1, TEST_FAIL_TRAP

finish:
    fence   rw, rw
    sd      t1, 0(s0)

secondary_hart:
    wfi
    j       secondary_hart
