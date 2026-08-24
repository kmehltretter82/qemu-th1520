/*
 * RISC-V inhibited PMU counter migration guest.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    .equ TEST_STATUS,             TEST_STATUS_ADDR
    .equ TEST_ARM,                TEST_STATUS + 8
    .equ TEST_GO,                 TEST_STATUS + 16
    .equ TEST_SOURCE_COUNT,       TEST_STATUS + 24
    .equ TEST_DEST_COUNT,         TEST_STATUS + 32
    .equ TEST_STATUS_READY,       1
    .equ TEST_STATUS_ARMED,       2
    .equ TEST_STATUS_PASS,        3
    .equ TEST_FAIL_TRAP,          0xdead2001
    .equ TEST_FAIL_SOURCE,        0xdead2002
    .equ TEST_FAIL_INHIBIT,       0xdead2003
    .equ TEST_FAIL_DEST,          0xdead2004
    .equ TEST_FAIL_SELECTOR,      0xdead2005
    .equ TEST_FAIL_RESTART,       0xdead2006
    .equ PMU_COUNTER3_BIT,        0x8
    .equ PMU_INITIAL_VALUE,       0x123456789

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
    lla     t0, unexpected_trap
    csrw    mtvec, t0

    li      t0, PMU_COUNTER3_BIT
    csrs    mcountinhibit, t0
    li      t1, PMU_EVENT_VALUE
    csrw    mhpmevent3, t1
    li      t1, PMU_INITIAL_VALUE
    csrw    mhpmcounter3, t1
#ifdef C910_PMU
    csrw    0x7cb, zero            /* th.mcounterof */
#endif

    li      t1, TEST_STATUS_READY
    fence   rw, rw
    sd      t1, 0(s0)

wait_for_arm:
    ld      t1, TEST_ARM - TEST_STATUS(s0)
    beqz    t1, wait_for_arm

    /* An inhibited counter must remain exact despite guest execution. */
    li      t2, 0x10000
1:
    addi    t2, t2, -1
    bnez    t2, 1b
    csrr    t1, mhpmcounter3
    li      t2, PMU_INITIAL_VALUE
    bne     t1, t2, fail_source
    sd      t1, TEST_SOURCE_COUNT - TEST_STATUS(s0)

    li      t1, TEST_STATUS_ARMED
    fence   rw, rw
    sd      t1, 0(s0)

wait_for_go:
    ld      t1, TEST_GO - TEST_STATUS(s0)
    beqz    t1, wait_for_go

    csrr    t1, mcountinhibit
    andi    t1, t1, PMU_COUNTER3_BIT
    beqz    t1, fail_inhibit
    csrr    t1, mhpmevent3
    li      t2, PMU_EVENT_VALUE
    bne     t1, t2, fail_selector
    csrr    t1, mhpmcounter3
    ld      t2, TEST_SOURCE_COUNT - TEST_STATUS(s0)
    bne     t1, t2, fail_dest
    sd      t1, TEST_DEST_COUNT - TEST_STATUS(s0)

    li      t2, 0x10000
2:
    addi    t2, t2, -1
    bnez    t2, 2b
    csrr    t1, mhpmcounter3
    ld      t2, TEST_DEST_COUNT - TEST_STATUS(s0)
    bne     t1, t2, fail_dest

    li      t0, PMU_COUNTER3_BIT
    csrc    mcountinhibit, t0
    li      t2, 0x10000
3:
    addi    t2, t2, -1
    bnez    t2, 3b
    csrs    mcountinhibit, t0
    csrr    t1, mhpmcounter3
    ld      t2, TEST_DEST_COUNT - TEST_STATUS(s0)
    bleu    t1, t2, fail_restart

    li      t1, TEST_STATUS_PASS
    j       finish

fail_source:
    li      t1, TEST_FAIL_SOURCE
    j       finish
fail_inhibit:
    li      t1, TEST_FAIL_INHIBIT
    j       finish
fail_dest:
    li      t1, TEST_FAIL_DEST
    j       finish
fail_selector:
    li      t1, TEST_FAIL_SELECTOR
    j       finish
fail_restart:
    li      t1, TEST_FAIL_RESTART
    j       finish
unexpected_trap:
    li      t1, TEST_FAIL_TRAP

finish:
    fence   rw, rw
    sd      t1, 0(s0)

secondary_hart:
    wfi
    j       secondary_hart
