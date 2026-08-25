/*
 * T-Head C910 FXCR floating-point control/status test.
 *
 * FXCR is the C910 user CSR at 0x800.  It aliases frm/fflags and adds
 * DQNaN (NaN propagation policy) plus FE (an aggregate exception event).
 * The test deliberately executes in M/S/U modes and uses both scalar and
 * frozen XTheadVector instructions so a register-only implementation cannot
 * pass.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.option	norelax
	.option	norvc

#define CSR_FFLAGS		0x001
#define CSR_FRM			0x002
#define CSR_FCSR		0x003
#define CSR_TH_FXCR		0x800

#define MSTATUS_MPP_MASK	(3 << 11)
#define MSTATUS_MPP_M		(3 << 11)
#define MSTATUS_MPP_S		(1 << 11)
#define MSTATUS_MPP_U		0
#define MSTATUS_FS_INITIAL	(1 << 13)
#define MSTATUS_FS_CLEAN	(2 << 13)
#define MSTATUS_FS_DIRTY	(3 << 13)
#define MSTATUS_FS_MASK		(3 << 13)
#define MSTATUS_VS_THEAD	(1 << 23)

#define FXCR_FRM_SHIFT		24
#define FXCR_DQNAN		(1 << 23)
#define FXCR_FE			(1 << 5)
#define FFLAGS_NX		(1 << 0)
#define FFLAGS_UF		(1 << 1)
#define FFLAGS_OF		(1 << 2)
#define FFLAGS_DZ		(1 << 3)
#define FFLAGS_NV		(1 << 4)

#define CAUSE_ILLEGAL		2
#define CAUSE_ECALL_U		8
#define CAUSE_ECALL_S		9

/*
 * Persistent registers:
 *   s0 = expected trap cause (zero means no trap is expected)
 *   s1 = expected trap PC
 *   s2 = failure stage
 *   s3 = M-mode continuation after a lower-mode phase
 *   s4 = lower-mode result (one = pass, two = fail)
 *   s5 = accepted expected-trap count
 */

	.macro	SET_MPP value
	csrr	t0, mstatus
	li	t1, MSTATUS_MPP_MASK
	not	t1, t1
	and	t0, t0, t1
	li	t1, \value
	or	t0, t0, t1
	csrw	mstatus, t0
	.endm

	.macro	SET_FS value
	li	t0, MSTATUS_FS_MASK
	csrc	mstatus, t0
	li	t0, \value
	csrs	mstatus, t0
	.endm

	.macro	EXPECT_ILLEGAL word, fail_target
	li	s0, CAUSE_ILLEGAL
	lla	s1, .Lexpected_\@
.Lexpected_\@:
	.word	\word
	bnez	s0, \fail_target
	.endm

	.macro	CHECK_FXCR value, fail_target
	csrr	t0, CSR_TH_FXCR
	li	t1, \value
	bne	t0, t1, \fail_target
	.endm

	.macro	LOAD_S freg, value
	li	t0, \value
	fmv.w.x	\freg, t0
	.endm

	.macro	CHECK_S freg, value, fail_target
	fmv.x.w	t0, \freg
	slli	t0, t0, 32
	srli	t0, t0, 32
	li	t1, \value
	bne	t0, t1, \fail_target
	.endm

	.macro	LOAD_H freg, value
	li	t0, \value
	fmv.h.x	\freg, t0
	.endm

	.macro	CHECK_H freg, value, fail_target
	fmv.x.h	t0, \freg
	slli	t0, t0, 48
	srli	t0, t0, 48
	li	t1, \value
	bne	t0, t1, \fail_target
	.endm

	.macro	LOAD_D freg, value
	li	t0, \value
	fmv.d.x	\freg, t0
	.endm

	.macro	CHECK_D freg, value, fail_target
	fmv.x.d	t0, \freg
	li	t1, \value
	bne	t0, t1, \fail_target
	.endm

	.text
	.global	_start
_start:
	csrr	t0, mhartid
	beqz	t0, 1f
0:
	wfi
	j	0b
1:
	lla	t0, mtrap
	csrw	mtvec, t0
	csrw	medeleg, zero
	li	s0, 0
	li	s4, 0
	li	s5, 0

	/* FXCR reads and writes are illegal while FS is Off in M mode. */
	li	s2, 1
	SET_FS	0
	EXPECT_ILLEGAL 0x800022f3, fail /* csrr t0, 0x800 */
	EXPECT_ILLEGAL 0x80029073, fail /* csrw 0x800, t0 */

	/*
	 * A read from Initial state is clean; even a reserved-only write
	 * dirties.
	 */
	li	s2, 2
	SET_FS	MSTATUS_FS_INITIAL
	csrr	t0, CSR_TH_FXCR
	bnez	t0, fail
	csrr	t0, mstatus
	li	t1, MSTATUS_FS_MASK
	and	t0, t0, t1
	li	t1, MSTATUS_FS_INITIAL
	bne	t0, t1, fail

	SET_FS	MSTATUS_FS_CLEAN
	csrr	t0, CSR_TH_FXCR
	bnez	t0, fail
	csrr	t0, mstatus
	li	t1, MSTATUS_FS_MASK
	and	t0, t0, t1
	li	t1, MSTATUS_FS_CLEAN
	bne	t0, t1, fail
	li	t0, 1
	slli	t0, t0, 63
	csrw	CSR_TH_FXCR, t0
	CHECK_FXCR 0, fail
	csrr	t0, mstatus
	li	t1, MSTATUS_FS_MASK
	and	t0, t0, t1
	li	t1, MSTATUS_FS_DIRTY
	bne	t0, t1, fail

	/* The exact mask is RM[26:24], DQNaN, FE, and fflags[4:0]. */
	li	s2, 3
	li	t0, -1
	csrw	CSR_TH_FXCR, t0
	CHECK_FXCR 0x0780003f, fail
	csrr	t0, CSR_FRM
	li	t1, 7
	bne	t0, t1, fail
	csrr	t0, CSR_FFLAGS
	li	t1, 0x1f
	bne	t0, t1, fail
	csrr	t0, CSR_FCSR
	li	t1, 0xff
	bne	t0, t1, fail

	/* Standard CSR writes update aliases without touching DQNaN or FE. */
	li	s2, 4
	li	t0, 5
	csrw	CSR_FFLAGS, t0
	CHECK_FXCR 0x07800025, fail
	li	t0, 2
	csrw	CSR_FRM, t0
	CHECK_FXCR 0x02800025, fail
	li	t0, 0x83
	csrw	CSR_FCSR, t0
	CHECK_FXCR 0x04800023, fail

	/* Software-created flags do not create an FE event. */
	li	s2, 5
	li	t0, FXCR_DQNAN
	csrw	CSR_TH_FXCR, t0
	li	t0, FFLAGS_NV
	csrw	CSR_FFLAGS, t0
	CHECK_FXCR (FXCR_DQNAN | FFLAGS_NV), fail
	li	t0, 3
	csrw	CSR_FRM, t0
	CHECK_FXCR ((3 << FXCR_FRM_SHIFT) | FXCR_DQNAN | FFLAGS_NV), fail
	li	t0, 0x45
	csrw	CSR_FCSR, t0
	CHECK_FXCR ((2 << FXCR_FRM_SHIFT) | FXCR_DQNAN | 5), fail

	/* FS Off also gates FXCR in S and U modes. */
	li	s2, 6
	SET_FS	0
	lla	s3, after_s_off
	lla	t0, s_off
	csrw	mepc, t0
	SET_MPP	MSTATUS_MPP_S
	mret
after_s_off:
	li	s2, 7
	lla	s3, after_u_off
	lla	t0, u_off
	csrw	mepc, t0
	SET_MPP	MSTATUS_MPP_U
	mret
after_u_off:

	/* With FS enabled the user CSR is writable in both lower modes. */
	li	s2, 8
	SET_FS	MSTATUS_FS_INITIAL
	lla	s3, after_s_rw
	lla	t0, s_rw
	csrw	mepc, t0
	SET_MPP	MSTATUS_MPP_S
	mret
after_s_rw:
	CHECK_FXCR 0x01800021, fail

	li	s2, 9
	lla	s3, after_u_rw
	lla	t0, u_rw
	csrw	mepc, t0
	SET_MPP	MSTATUS_MPP_U
	mret
after_u_rw:
	CHECK_FXCR 0x02800022, fail

	/* Reserved dynamic rounding modes latch but trap when consumed. */
	li	s2, 10
	LOAD_S	ft0, 0x3f800000
	LOAD_S	ft1, 0x3f800000
	li	t0, (5 << FXCR_FRM_SHIFT)
	csrw	CSR_TH_FXCR, t0
	CHECK_FXCR (5 << FXCR_FRM_SHIFT), fail
	EXPECT_ILLEGAL 0x00107153, fail /* fadd.s ft2, ft0, ft1, dyn */
	li	t0, (6 << FXCR_FRM_SHIFT)
	csrw	CSR_TH_FXCR, t0
	CHECK_FXCR (6 << FXCR_FRM_SHIFT), fail
	EXPECT_ILLEGAL 0x00107153, fail
	li	t0, (7 << FXCR_FRM_SHIFT)
	csrw	CSR_TH_FXCR, t0
	CHECK_FXCR (7 << FXCR_FRM_SHIFT), fail
	EXPECT_ILLEGAL 0x00107153, fail

	/* DQNaN=0 canonicalizes a quiet NaN; DQNaN=1 propagates it. */
	li	s2, 20
	csrw	CSR_TH_FXCR, zero
	LOAD_S	ft0, 0xffc12345
	LOAD_S	ft1, 0x3f800000
	fadd.s	ft2, ft0, ft1, rne
	CHECK_S	ft2, 0x7fc00000, fail
	CHECK_FXCR 0, fail

	li	s2, 21
	li	t0, FXCR_DQNAN
	csrw	CSR_TH_FXCR, t0
	fadd.s	ft2, ft0, ft1, rne
	CHECK_S	ft2, 0xffc12345, fail
	CHECK_FXCR FXCR_DQNAN, fail

	/* Binary propagation chooses qNaN A, but any sNaN precedes qNaNs. */
	li	s2, 22
	LOAD_S	ft0, 0x7fc11111
	LOAD_S	ft1, 0xffc22222
	fadd.s	ft2, ft0, ft1, rne
	CHECK_S	ft2, 0x7fc11111, fail
	fdiv.s	ft2, ft0, ft1, rne
	CHECK_S	ft2, 0x7fc11111, fail

	li	s2, 23
	li	t0, FXCR_DQNAN
	csrw	CSR_TH_FXCR, t0
	LOAD_S	ft0, 0x7fc11111
	LOAD_S	ft1, 0xff822222
	fadd.s	ft2, ft0, ft1, rne
	CHECK_S	ft2, 0xffc22222, fail
	CHECK_FXCR (FXCR_DQNAN | FXCR_FE | FFLAGS_NV), fail

	/* FMA qNaN priority is A, then B, then C. */
	li	s2, 24
	li	t0, FXCR_DQNAN
	csrw	CSR_TH_FXCR, t0
	LOAD_S	ft0, 0x7fc11111
	LOAD_S	ft1, 0xffc22222
	LOAD_S	ft2, 0x7fc33333
	fmadd.s	ft3, ft0, ft1, ft2, rne
	CHECK_S	ft3, 0x7fc11111, fail

	li	s2, 25
	LOAD_S	ft0, 0x3f800000
	fmadd.s	ft3, ft0, ft1, ft2, rne
	CHECK_S	ft3, 0xffc22222, fail

	li	s2, 26
	LOAD_S	ft1, 0x40000000
	fmadd.s	ft3, ft0, ft1, ft2, rne
	CHECK_S	ft3, 0x7fc33333, fail

	/* A signaling C operand wins over a quiet A and is silenced with NV. */
	li	s2, 27
	li	t0, FXCR_DQNAN
	csrw	CSR_TH_FXCR, t0
	LOAD_S	ft0, 0x7fc11111
	LOAD_S	ft1, 0x3f800000
	LOAD_S	ft2, 0xff833333
	fmadd.s	ft3, ft0, ft1, ft2, rne
	CHECK_S	ft3, 0xffc33333, fail
	CHECK_FXCR (FXCR_DQNAN | FXCR_FE | FFLAGS_NV), fail

	/* 0 * Inf + qNaN C propagates C and still raises invalid. */
	li	s2, 28
	li	t0, FXCR_DQNAN
	csrw	CSR_TH_FXCR, t0
	LOAD_S	ft0, 0
	LOAD_S	ft1, 0x7f800000
	LOAD_S	ft2, 0xffc44444
	fmadd.s	ft3, ft0, ft1, ft2, rne
	CHECK_S	ft3, 0xffc44444, fail
	CHECK_FXCR (FXCR_DQNAN | FXCR_FE | FFLAGS_NV), fail

	/* Format conversion follows the same canonical/propagating policy. */
	li	s2, 29
	li	t0, FXCR_DQNAN
	csrw	CSR_TH_FXCR, t0
	LOAD_S	ft0, 0xffc12345
	fcvt.d.s	ft2, ft0
	fmv.x.d	t0, ft2
	li	t1, 0xfff82468a0000000
	bne	t0, t1, fail
	csrw	CSR_TH_FXCR, zero
	fcvt.d.s	ft2, ft0
	fmv.x.d	t0, ft2
	li	t1, 0x7ff8000000000000
	bne	t0, t1, fail

	/* Half and double arithmetic use the same DQNaN polarity. */
	li	s2, 30
	csrw	CSR_TH_FXCR, zero
	LOAD_H	ft0, 0xfe55
	LOAD_H	ft1, 0x3c00
	fadd.h	ft2, ft0, ft1, rne
	CHECK_H	ft2, 0x7e00, fail
	li	t0, FXCR_DQNAN
	csrw	CSR_TH_FXCR, t0
	fadd.h	ft2, ft0, ft1, rne
	CHECK_H	ft2, 0xfe55, fail

	li	s2, 31
	csrw	CSR_TH_FXCR, zero
	LOAD_D	ft0, 0xfff8123456789abc
	LOAD_D	ft1, 0x3ff0000000000000
	fadd.d	ft2, ft0, ft1, rne
	CHECK_D	ft2, 0x7ff8000000000000, fail
	li	t0, FXCR_DQNAN
	csrw	CSR_TH_FXCR, t0
	fadd.d	ft2, ft0, ft1, rne
	CHECK_D	ft2, 0xfff8123456789abc, fail

	/* XTheadVector arithmetic observes DQNaN too. */
	li	s2, 32
	li	t0, MSTATUS_VS_THEAD
	csrs	mstatus, t0
	li	t1, 1
	.word	0x00837057	/* th.vsetvli zero, t1, e32, m1 */
	lla	t2, vector_qnan
	.word	0x0203f087	/* th.vle.v v1, (t2) */
	lla	t2, vector_one
	.word	0x0203f107	/* th.vle.v v2, (t2) */
	li	t0, FXCR_DQNAN
	csrw	CSR_TH_FXCR, t0
	.word	0x021111d7	/* th.vfadd.vv v3, v1, v2 */
	lla	t2, vector_result
	.word	0x0203f1a7	/* th.vse.v v3, (t2) */
	lwu	t0, 0(t2)
	li	t1, 0xffc55555
	bne	t0, t1, fail
	csrw	CSR_TH_FXCR, zero
	.word	0x021111d7
	.word	0x0203f1a7
	lwu	t0, 0(t2)
	li	t1, 0x7fc00000
	bne	t0, t1, fail

	/* FE records each IEEE exception class. */
	LOAD_S	ft0, 0
	LOAD_S	ft1, 0x3f800000
	LOAD_S	ft2, 0x40000000
	LOAD_S	ft3, 0x7f7fffff
	LOAD_S	ft4, 0x00800000
	LOAD_S	ft5, 0x33800000

	li	s2, 40
	csrw	CSR_TH_FXCR, zero
	fdiv.s	ft6, ft0, ft0, rne
	CHECK_FXCR (FXCR_FE | FFLAGS_NV), fail

	li	s2, 41
	csrw	CSR_TH_FXCR, zero
	fdiv.s	ft6, ft1, ft0, rne
	CHECK_FXCR (FXCR_FE | FFLAGS_DZ), fail

	li	s2, 42
	csrw	CSR_TH_FXCR, zero
	fmul.s	ft6, ft3, ft2, rne
	CHECK_FXCR (FXCR_FE | FFLAGS_OF | FFLAGS_NX), fail

	li	s2, 43
	csrw	CSR_TH_FXCR, zero
	fmul.s	ft6, ft4, ft4, rne
	CHECK_FXCR (FXCR_FE | FFLAGS_UF | FFLAGS_NX), fail

	li	s2, 44
	csrw	CSR_TH_FXCR, zero
	fadd.s	ft6, ft1, ft5, rne
	CHECK_FXCR (FXCR_FE | FFLAGS_NX), fail

	/*
	 * An exact operation with pre-existing NX must leave FE clear.  Raising
	 * that already-sticky NX event again must set FE; this also exercises
	 * the host-fast-path guard needed for precise event tracking.
	 */
	li	s2, 45
	li	t0, FFLAGS_NX
	csrw	CSR_TH_FXCR, t0
	fadd.s	ft6, ft1, ft1, rne
	CHECK_FXCR FFLAGS_NX, fail
	fadd.s	ft6, ft1, ft5, rne
	CHECK_FXCR (FXCR_FE | FFLAGS_NX), fail

	/* A GPR-result operation that changes only FE must still dirty FS. */
	li	s2, 46
	LOAD_S	ft0, 0x7fc11111
	LOAD_S	ft1, 0x3f800000
	li	t0, FFLAGS_NV
	csrw	CSR_TH_FXCR, t0
	SET_FS	MSTATUS_FS_CLEAN
	flt.s	t2, ft0, ft1
	bnez	t2, fail
	csrr	t0, mstatus
	li	t1, MSTATUS_FS_MASK
	and	t0, t0, t1
	li	t1, MSTATUS_FS_DIRTY
	bne	t0, t1, fail
	CHECK_FXCR (FXCR_FE | FFLAGS_NV), fail

	/* Clearing architectural flags cannot erase a pending retired event. */
	li	s2, 47
	li	t0, FXCR_DQNAN
	csrw	CSR_TH_FXCR, t0
	LOAD_S	ft0, 0x7f811111
	fdiv.s	ft6, ft1, ft0, rne
	csrw	CSR_FFLAGS, zero
	CHECK_FXCR (FXCR_DQNAN | FXCR_FE), fail
	csrw	CSR_FCSR, zero
	CHECK_FXCR (FXCR_DQNAN | FXCR_FE), fail

	li	s2, 48
	li	t0, 9
	bne	s5, t0, fail
	li	a0, 0
	j	exit

s_off:
	EXPECT_ILLEGAL 0x800022f3, lower_fail
	EXPECT_ILLEGAL 0x80029073, lower_fail
	li	s4, 1
	ecall
	j	.

u_off:
	EXPECT_ILLEGAL 0x800022f3, lower_fail
	EXPECT_ILLEGAL 0x80029073, lower_fail
	li	s4, 1
	ecall
	j	.

s_rw:
	li	t0, 0x01800021
	csrw	CSR_TH_FXCR, t0
	CHECK_FXCR 0x01800021, lower_fail
	li	s4, 1
	ecall
	j	.

u_rw:
	li	t0, 0x02800022
	csrw	CSR_TH_FXCR, t0
	CHECK_FXCR 0x02800022, lower_fail
	li	s4, 1
	ecall
	j	.

lower_fail:
	li	s4, 2
	ecall
	j	.

	.balign	4
mtrap:
	csrr	t0, mcause
	beqz	s0, lower_trap
	bne	t0, s0, wrong_cause
	csrr	t0, mepc
	bne	t0, s1, wrong_pc
	li	s0, 0
	addi	s5, s5, 1
	addi	t0, t0, 4
	csrw	mepc, t0
	mret

lower_trap:
	li	t1, CAUSE_ECALL_U
	beq	t0, t1, lower_return
	li	t1, CAUSE_ECALL_S
	bne	t0, t1, unexpected_trap
lower_return:
	li	t0, 1
	bne	s4, t0, fail
	li	s4, 0
	csrw	mepc, s3
	SET_MPP	MSTATUS_MPP_M
	mret

wrong_cause:
	li	s2, 91
	j	fail
wrong_pc:
	li	s2, 92
	j	fail
unexpected_trap:
	li	s2, 90

fail:
	mv	a0, s2
exit:
	lla	a1, semiargs
	li	t0, 0x20026	/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	a0, 8(a1)
	li	a0, 0x20	/* TARGET_SYS_EXIT_EXTENDED */
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

	.section .rodata
	.balign	16
vector_qnan:
	.word	0xffc55555
	.space	12
vector_one:
	.word	0x3f800000
	.space	12

	.data
	.balign	16
vector_result:
	.space	16
semiargs:
	.space	16
