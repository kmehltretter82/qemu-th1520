/*
 * T-Head C910 legacy FXCR migration guest.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .option norvc

    /* Public mailbox protocol, mirrored by riscv-csr-test.c. */
    .equ TEST_STATUS,              0x200000
    .equ TEST_GO,                  TEST_STATUS + 8
    .equ TEST_STATUS_READY,        1
    .equ TEST_STATUS_PASS,         2

    .equ TEST_FAIL_FFLAGS,         0xdead3101
    .equ TEST_FAIL_DQNAN,          0xdead3102
    .equ TEST_FAIL_FS_DIRTY,       0xdead3103
    .equ TEST_FAIL_FXCR,           0xdead3104
    .equ TEST_FAIL_TRAP,           0xdead31ff

    .equ CSR_FFLAGS,               0x001
    .equ CSR_FXCR,                 0x800
    .equ FFLAGS_NX,                0x01
    .equ FXCR_FE,                  0x20
    .equ FXCR_FRM_RUP,             0x03000000
    .equ FXCR_RESULT,              FXCR_FRM_RUP | FXCR_FE | FFLAGS_NX

    .equ MSTATUS_FS,               0x6000
    .equ MSTATUS_FS_INITIAL,       0x2000
    .equ MSTATUS_FS_CLEAN,         0x4000
    .equ MSTATUS_FS_DIRTY,         0x6000

    .equ FLOAT32_QNAN_PAYLOAD,     0x7fc11111
    .equ FLOAT32_QNAN_CANONICAL,   0x7fc00000
    .equ FLOAT32_ONE,              0x3f800000
    .equ FLOAT32_HALF_ULP_AT_ONE,  0x33800000

    .text
    .global _start
_start:
    csrr    t0, mhartid
    bnez    t0, secondary_hart
    li      s0, TEST_STATUS
    lla     t0, unexpected_trap
    csrw    mtvec, t0
    sd      zero, 0(s0)
    sd      zero, TEST_GO - TEST_STATUS(s0)

    /* Use only standard F state while constructing the source VM. */
    li      t0, MSTATUS_FS_INITIAL
    csrs    mstatus, t0
    li      t0, FLOAT32_QNAN_PAYLOAD
    fmv.w.x ft0, t0
    li      t0, FLOAT32_ONE
    fmv.w.x ft1, t0
    li      t0, FLOAT32_HALF_ULP_AT_ONE
    fmv.w.x ft2, t0

    li      t0, TEST_STATUS_READY
    fence   rw, rw
    sd      t0, 0(s0)

wait_for_go:
    ld      t0, TEST_GO - TEST_STATUS(s0)
    beqz    t0, wait_for_go

    /* Legacy C910 streams have no vendor FFLAGS payload. */
    csrr    t0, CSR_FFLAGS
    bnez    t0, fail_fflags

    /* Legacy DQNaN defaults to zero, selecting the canonical NaN. */
    fadd.s  ft3, ft0, ft1
    fmv.x.w t0, ft3
    li      t1, FLOAT32_QNAN_CANONICAL
    bne     t0, t1, fail_dqnan

    /*
     * Seed sticky NX without creating an exception event, then raise NX
     * again.  This proves post-load re-armed the C910 FE event tracker.
     */
    li      t0, FFLAGS_NX
    csrw    CSR_FFLAGS, t0
    csrr    t0, mstatus
    li      t1, MSTATUS_FS
    not     t1, t1
    and     t0, t0, t1
    li      t1, MSTATUS_FS_CLEAN
    or      t0, t0, t1
    csrw    mstatus, t0

    fadd.s  ft4, ft1, ft2, rne

    csrr    t0, mstatus
    li      t1, MSTATUS_FS
    and     t0, t0, t1
    li      t1, MSTATUS_FS_DIRTY
    bne     t0, t1, fail_fs_dirty

    /* This is intentionally the first FXCR access after migration. */
    csrr    t0, CSR_FXCR
    li      t1, FXCR_RESULT
    bne     t0, t1, fail_fxcr

    li      t0, TEST_STATUS_PASS
    j       finish

fail_fflags:
    li      t0, TEST_FAIL_FFLAGS
    j       finish
fail_dqnan:
    li      t0, TEST_FAIL_DQNAN
    j       finish
fail_fs_dirty:
    li      t0, TEST_FAIL_FS_DIRTY
    j       finish
fail_fxcr:
    li      t0, TEST_FAIL_FXCR
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
