/*
 * RISC-V fixed-counter-only PMU migration guest.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ TEST_STATUS,             0x80200000
    .equ TEST_ARM,                TEST_STATUS + 8
    .equ TEST_GO,                 TEST_STATUS + 16
    .equ TEST_SOURCE_CYCLE,       TEST_STATUS + 24
    .equ TEST_SOURCE_INSTRET,     TEST_STATUS + 32
    .equ TEST_DEST_CYCLE,         TEST_STATUS + 40
    .equ TEST_DEST_INSTRET,       TEST_STATUS + 48
    .equ TEST_MIGRATED,           TEST_STATUS + 56
    .equ TEST_STATUS_READY,       1
    .equ TEST_STATUS_ARMED,       2
    .equ TEST_STATUS_PASS,        3
    .equ TEST_FAIL_TRAP,          0xdead1001
    .equ TEST_FAIL_CONFIG,        0xdead1002
    .equ TEST_FAIL_CYCLE_CONT,    0xdead1003
    .equ TEST_FAIL_INSTRET_CONT,  0xdead1004
    .equ TEST_FAIL_CYCLE_COUNT,   0xdead1005
    .equ TEST_FAIL_INSTRET_COUNT, 0xdead1006
    .equ FIXED_COUNTER_BITS,      5
    .equ MAX_MIGRATION_DELTA,     0x1000

    .text
    .global _start
_start:
    csrr    t0, mhartid
    bnez    t0, secondary_hart

    li      s0, TEST_STATUS
    sd      zero, 0(s0)
    sd      zero, TEST_ARM - TEST_STATUS(s0)
    sd      zero, TEST_GO - TEST_STATUS(s0)
    sd      zero, TEST_SOURCE_CYCLE - TEST_STATUS(s0)
    sd      zero, TEST_SOURCE_INSTRET - TEST_STATUS(s0)
    sd      zero, TEST_DEST_CYCLE - TEST_STATUS(s0)
    sd      zero, TEST_DEST_INSTRET - TEST_STATUS(s0)
    sd      zero, TEST_MIGRATED - TEST_STATUS(s0)
    lla     t0, trap_handler
    csrw    mtvec, t0

    /* Allow the filtered U-mode workload to execute from DRAM. */
    li      t0, -1
    csrw    pmpaddr0, t0
    li      t0, 0xf
    csrw    pmpcfg0, t0

    /* Exercise the fixed-counter path without any programmable counters. */
    li      t0, FIXED_COUNTER_BITS
    csrs    mcountinhibit, t0
    li      t1, 1
    slli    t1, t1, 60
    csrw    0x321, t1             /* mcyclecfg: inhibit U-mode only */
    csrw    0x322, t1             /* minstretcfg: inhibit U-mode only */
    li      t1, 0x12340000
    csrw    mcycle, t1
    li      t1, 0x56780000
    csrw    minstret, t1

    li      t1, TEST_STATUS_READY
    fence   rw, rw
    sd      t1, 0(s0)

wait_for_arm:
    ld      t1, TEST_ARM - TEST_STATUS(s0)
    beqz    t1, wait_for_arm

    li      t0, FIXED_COUNTER_BITS
    csrc    mcountinhibit, t0

    /*
     * Run a large filtered U-mode interval.  That interval is represented in
     * the process-local fixed-counter accounting, but not in global icount.
     */
    lla     t0, user_work
    csrw    mepc, t0
    li      t0, 0x1800
    csrc    mstatus, t0
    mret

user_work:
    li      t2, 0x100000
1:
    addi    t2, t2, -1
    bnez    t2, 1b
    ecall

after_user:
    li      t2, 0x1000
2:
    addi    t2, t2, -1
    bnez    t2, 2b
    csrr    t1, mcycle
    sd      t1, TEST_SOURCE_CYCLE - TEST_STATUS(s0)
    csrr    t1, minstret
    sd      t1, TEST_SOURCE_INSTRET - TEST_STATUS(s0)

    li      t1, TEST_STATUS_ARMED
    fence   rw, rw
    sd      t1, 0(s0)

wait_for_go:
    ld      t0, TEST_MIGRATED - TEST_STATUS(s0)
    bnez    t0, destination_check
    /* Keep the source snapshot within a few instructions of host stop. */
    csrr    t1, mcycle
    sd      t1, TEST_SOURCE_CYCLE - TEST_STATUS(s0)
    csrr    t1, minstret
    sd      t1, TEST_SOURCE_INSTRET - TEST_STATUS(s0)
    ld      t1, TEST_GO - TEST_STATUS(s0)
    beqz    t1, wait_for_go

destination_check:
    li      t2, 1
    slli    t2, t2, 60
    csrr    t1, 0x321
    bne     t1, t2, fail_config
    csrr    t1, 0x322
    bne     t1, t2, fail_config

    csrr    t1, mcycle
    ld      t2, TEST_SOURCE_CYCLE - TEST_STATUS(s0)
    bltu    t1, t2, fail_cycle_continuity
    sub     t3, t1, t2
    li      t4, MAX_MIGRATION_DELTA
    bgtu    t3, t4, fail_cycle_continuity
    sd      t1, TEST_DEST_CYCLE - TEST_STATUS(s0)
    csrr    t1, minstret
    ld      t2, TEST_SOURCE_INSTRET - TEST_STATUS(s0)
    bltu    t1, t2, fail_instret_continuity
    sub     t3, t1, t2
    li      t4, MAX_MIGRATION_DELTA
    bgtu    t3, t4, fail_instret_continuity
    sd      t1, TEST_DEST_INSTRET - TEST_STATUS(s0)

    li      t2, 0x1000
2:
    addi    t2, t2, -1
    bnez    t2, 2b

    li      t0, FIXED_COUNTER_BITS
    csrs    mcountinhibit, t0
    csrr    t1, mcycle
    ld      t2, TEST_DEST_CYCLE - TEST_STATUS(s0)
    beq     t1, t2, fail_cycle_count
    csrr    t1, minstret
    ld      t2, TEST_DEST_INSTRET - TEST_STATUS(s0)
    beq     t1, t2, fail_instret_count

    li      t1, TEST_STATUS_PASS
    j       finish

fail_config:
    li      t1, TEST_FAIL_CONFIG
    j       finish
fail_cycle_continuity:
    li      t1, TEST_FAIL_CYCLE_CONT
    j       finish
fail_instret_continuity:
    li      t1, TEST_FAIL_INSTRET_CONT
    j       finish
fail_cycle_count:
    li      t1, TEST_FAIL_CYCLE_COUNT
    j       finish
fail_instret_count:
    li      t1, TEST_FAIL_INSTRET_COUNT
    j       finish
unexpected_trap:
    li      t1, TEST_FAIL_TRAP
    j       finish

trap_handler:
    csrr    t0, mcause
    li      t1, 8                  /* Environment call from U-mode */
    bne     t0, t1, unexpected_trap
    lla     t0, after_user
    csrw    mepc, t0
    li      t0, 0x1800
    csrs    mstatus, t0            /* Return to M-mode. */
    mret

finish:
    fence   rw, rw
    sd      t1, 0(s0)

secondary_hart:
    wfi
    j       secondary_hart
