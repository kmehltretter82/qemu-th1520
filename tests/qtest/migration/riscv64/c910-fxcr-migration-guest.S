/*
 * T-Head C910 FXCR migration and reset guest.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    /* Public mailbox protocol, mirrored by riscv-csr-test.c. */
    .equ TEST_STATUS,                 0x200000
    .equ TEST_GO,                     TEST_STATUS + 8
    .equ TEST_RESET_COOKIE,           TEST_STATUS + 16
    .equ TEST_RESET_COOKIE_SET,       1
    .equ TEST_STATUS_READY,           1
    .equ TEST_STATUS_MIGRATION_PASS,  2
    .equ TEST_STATUS_RESET_PASS,      3

    .equ TEST_FAIL_MIGRATED_DQNAN,    0xdead3001
    .equ TEST_FAIL_MIGRATED_FXCR,     0xdead3002
    .equ TEST_FAIL_RESET_COOKIE,      0xdead3003
    .equ TEST_FAIL_RESET_FS_OFF,      0xdead3004
    .equ TEST_FAIL_RESET_COMPARE,     0xdead3005
    .equ TEST_FAIL_RESET_FS_DIRTY,    0xdead3006
    .equ TEST_FAIL_RESET_FXCR,        0xdead3007
    .equ TEST_FAIL_MIGRATED_STALE,    0xdead3008
    .equ TEST_FAIL_TRAP,              0xdead30ff

    .equ CSR_FFLAGS,                  0x001
    .equ CSR_FXCR,                    0x800
    .equ FFLAGS_NV,                   0x10
    .equ FFLAGS_NX,                   0x01
    .equ FXCR_FE,                     0x20
    .equ FXCR_DQNAN,                  0x00800000
    .equ FXCR_SOURCE,                 FXCR_DQNAN | FFLAGS_NX
    .equ FXCR_MIGRATED_RESULT,        FXCR_DQNAN | FXCR_FE | FFLAGS_NX
    .equ FXCR_RESET_RESULT,           FXCR_FE | FFLAGS_NV

    .equ MSTATUS_FS,                  0x6000
    .equ MSTATUS_FS_INITIAL,          0x2000
    .equ MSTATUS_FS_CLEAN,            0x4000
    .equ MSTATUS_FS_DIRTY,            0x6000

    .equ FLOAT32_QNAN_PAYLOAD,        0x7fc11111
    .equ FLOAT32_ONE,                 0x3f800000
    .equ FLOAT32_HALF_ULP_AT_ONE,     0x33800000

    .text
    .global _start
_start:
    csrr    t0, mhartid
    bnez    t0, secondary_hart

    li      s0, TEST_STATUS
    lla     t0, unexpected_trap
    csrw    mtvec, t0

    /* RAM survives the host-requested system reset and selects phase two. */
    ld      t0, TEST_RESET_COOKIE - TEST_STATUS(s0)
    beqz    t0, source_phase
    li      t1, TEST_RESET_COOKIE_SET
    bne     t0, t1, fail_reset_cookie
    j       reset_phase

source_phase:
    sd      zero, 0(s0)
    sd      zero, TEST_GO - TEST_STATUS(s0)
    sd      zero, TEST_RESET_COOKIE - TEST_STATUS(s0)

    li      t0, MSTATUS_FS_INITIAL
    csrs    mstatus, t0

    /* These registers and the vendor NaN mode must survive migration. */
    li      t0, FLOAT32_QNAN_PAYLOAD
    fmv.w.x ft0, t0
    li      t0, FLOAT32_ONE
    fmv.w.x ft1, t0
    li      t0, FLOAT32_HALF_ULP_AT_ONE
    fmv.w.x ft2, t0
    li      t0, FXCR_SOURCE
    csrw    CSR_FXCR, t0

    li      t0, TEST_STATUS_READY
    fence   rw, rw
    sd      t0, 0(s0)

wait_for_go:
    ld      t0, TEST_GO - TEST_STATUS(s0)
    beqz    t0, wait_for_go

    /* No destination-side raised event may survive the post-load hook. */
    csrr    t0, CSR_FXCR
    li      t1, FXCR_SOURCE
    bne     t0, t1, fail_migrated_stale

    /* DQNAN=1 propagates the first quiet-NaN operand's payload. */
    fadd.s  ft3, ft0, ft1
    fmv.x.w t0, ft3
    li      t1, FLOAT32_QNAN_PAYLOAD
    bne     t0, t1, fail_migrated_dqnan

    /* NX is already sticky, but this operation is a new exception event. */
    fadd.s  ft4, ft1, ft2, rne
    csrr    t0, CSR_FXCR
    li      t1, FXCR_MIGRATED_RESULT
    bne     t0, t1, fail_migrated_fxcr

    li      t0, TEST_STATUS_MIGRATION_PASS
    j       finish

reset_phase:
    sd      zero, TEST_RESET_COOKIE - TEST_STATUS(s0)

    /* Check architectural reset before any floating-point or FXCR access. */
    csrr    t0, mstatus
    li      t1, MSTATUS_FS
    and     t0, t0, t1
    bnez    t0, fail_reset_fs_off

    li      t0, MSTATUS_FS_INITIAL
    csrs    mstatus, t0
    li      t0, FLOAT32_QNAN_PAYLOAD
    fmv.w.x ft0, t0
    li      t0, FLOAT32_ONE
    fmv.w.x ft1, t0

    /* Seed sticky NV without touching FXCR or its event-tracking state. */
    li      t0, FFLAGS_NV
    csrw    CSR_FFLAGS, t0

    csrr    t0, mstatus
    li      t1, MSTATUS_FS
    not     t1, t1
    and     t0, t0, t1
    li      t1, MSTATUS_FS_CLEAN
    or      t0, t0, t1
    csrw    mstatus, t0

    /* Signaling comparison with a quiet NaN raises the already-sticky NV. */
    flt.s   t0, ft0, ft1
    bnez    t0, fail_reset_compare

    csrr    t0, mstatus
    li      t1, MSTATUS_FS
    and     t0, t0, t1
    li      t1, MSTATUS_FS_DIRTY
    bne     t0, t1, fail_reset_fs_dirty

    /* This is intentionally the first FXCR access after reset. */
    csrr    t0, CSR_FXCR
    li      t1, FXCR_RESET_RESULT
    bne     t0, t1, fail_reset_fxcr

    li      t0, TEST_STATUS_RESET_PASS
    j       finish

fail_migrated_dqnan:
    li      t0, TEST_FAIL_MIGRATED_DQNAN
    j       finish
fail_migrated_fxcr:
    li      t0, TEST_FAIL_MIGRATED_FXCR
    j       finish
fail_reset_cookie:
    li      t0, TEST_FAIL_RESET_COOKIE
    j       finish
fail_reset_fs_off:
    li      t0, TEST_FAIL_RESET_FS_OFF
    j       finish
fail_reset_compare:
    li      t0, TEST_FAIL_RESET_COMPARE
    j       finish
fail_reset_fs_dirty:
    li      t0, TEST_FAIL_RESET_FS_DIRTY
    j       finish
fail_reset_fxcr:
    li      t0, TEST_FAIL_RESET_FXCR
    j       finish
fail_migrated_stale:
    li      t0, TEST_FAIL_MIGRATED_STALE
    j       finish
unexpected_trap:
    li      t0, TEST_FAIL_TRAP

finish:
    fence   rw, rw
    sd      t0, 0(s0)
1:
    wfi
    j       1b

secondary_hart:
    wfi
    j       secondary_hart
