/*
 * T-Head C910 scalar extension availability and privilege test.
 *
 * This test covers every th.addsl immediate and every XTheadCmo encoding
 * across M/S/U privilege, MXSTATUS.THEADISAEE, and MXSTATUS.UCME.  Raw words
 * keep the test independent of assembler support for the vendor extensions.
 *
 * The frozen XTheadCmo specification and C910 manual make th.dcache.cva
 * M/S-only.  Published openC910 RTL appears to permit it in U mode when UCME
 * is set; physical TH1520 behavior therefore remains a hardware-validation
 * item rather than changing this architectural regression.
 *
 * Whole-cache operations make this a freestanding firmware test.  Do not run
 * it from a live operating system when comparing against physical hardware.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.option	norelax
	.option	norvc

#define CSR_TH_MXSTATUS		0x7c0
#define MXSTATUS_UCME		(1 << 16)
#define MXSTATUS_THEADISAEE	(1 << 22)

#define MSTATUS_MPP_MASK	(3 << 11)
#define MSTATUS_MPP_M		(3 << 11)
#define MSTATUS_MPP_S		(1 << 11)
#define MSTATUS_MPP_U		0

#define CAUSE_ILLEGAL		2
#define CAUSE_ECALL_U		8
#define CAUSE_ECALL_S		9

/*
 * Persistent registers:
 *   s0 = expected-illegal trap count
 *   s1 = expected cause (zero when no instruction trap is expected)
 *   s2 = expected trap PC
 *   s3 = failure stage
 *   s4 = M-mode continuation after a lower-mode phase
 *   s5 = lower-mode result (one = pass, two = fail)
 *   s7 = expected illegal-instruction mtval
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

	.macro	EXPECT_ILLEGAL word, fail_target
	li	s1, CAUSE_ILLEGAL
	li	s7, \word
	lla	s2, .Lexpected_\@
.Lexpected_\@:
	.word	\word
	bnez	s1, \fail_target
	.endm

	.macro	CHECK_ADDSL fail_target
	li	a0, 7
	li	a1, 11
	.word	0x00b5160b	/* th.addsl a2, a0, a1, 0 */
	li	t0, 18
	bne	a2, t0, \fail_target
	.word	0x02b5160b	/* th.addsl a2, a0, a1, 1 */
	li	t0, 29
	bne	a2, t0, \fail_target
	.word	0x04b5160b	/* th.addsl a2, a0, a1, 2 */
	li	t0, 51
	bne	a2, t0, \fail_target
	.word	0x06b5160b	/* th.addsl a2, a0, a1, 3 */
	li	t0, 95
	bne	a2, t0, \fail_target
	.endm

	.macro	CHECK_ADDSL_ILLEGAL fail_target
	li	a2, 0x12345
	EXPECT_ILLEGAL 0x00b5160b, \fail_target
	li	t0, 0x12345
	bne	a2, t0, \fail_target
	li	a2, 0x23456
	EXPECT_ILLEGAL 0x02b5160b, \fail_target
	li	t0, 0x23456
	bne	a2, t0, \fail_target
	li	a2, 0x34567
	EXPECT_ILLEGAL 0x04b5160b, \fail_target
	li	t0, 0x34567
	bne	a2, t0, \fail_target
	li	a2, 0x45678
	EXPECT_ILLEGAL 0x06b5160b, \fail_target
	li	t0, 0x45678
	bne	a2, t0, \fail_target
	.endm

	.macro	CMO_ALL_LEGAL
	.word	0x0010000b	/* th.dcache.call */
	.word	0x0030000b	/* th.dcache.ciall */
	.word	0x0020000b	/* th.dcache.iall */
	.word	0x0295000b	/* th.dcache.cpa a0 */
	.word	0x02b5000b	/* th.dcache.cipa a0 */
	.word	0x02a5000b	/* th.dcache.ipa a0 */
	.word	0x0255000b	/* th.dcache.cva a0 */
	.word	0x0275000b	/* th.dcache.civa a0 */
	.word	0x0265000b	/* th.dcache.iva a0 */
	.word	0x0215000b	/* th.dcache.csw a0 */
	.word	0x0235000b	/* th.dcache.cisw a0 */
	.word	0x0225000b	/* th.dcache.isw a0 */
	.word	0x0285000b	/* th.dcache.cpal1 a0 */
	.word	0x0245000b	/* th.dcache.cval1 a0 */
	.word	0x0100000b	/* th.icache.iall */
	.word	0x0110000b	/* th.icache.ialls */
	.word	0x0385000b	/* th.icache.ipa a0 */
	.word	0x0305000b	/* th.icache.iva a0 */
	.word	0x0150000b	/* th.l2cache.call */
	.word	0x0170000b	/* th.l2cache.ciall */
	.word	0x0160000b	/* th.l2cache.iall */
	.endm

	.macro	CMO_U_LEGAL
	.word	0x0275000b	/* th.dcache.civa a0 */
	.word	0x0245000b	/* th.dcache.cval1 a0 */
	.word	0x0305000b	/* th.icache.iva a0 */
	.endm

	.macro	CMO_U_MS_ONLY fail_target
	EXPECT_ILLEGAL 0x0010000b, \fail_target
	EXPECT_ILLEGAL 0x0030000b, \fail_target
	EXPECT_ILLEGAL 0x0020000b, \fail_target
	EXPECT_ILLEGAL 0x0295000b, \fail_target
	EXPECT_ILLEGAL 0x02b5000b, \fail_target
	EXPECT_ILLEGAL 0x02a5000b, \fail_target
	EXPECT_ILLEGAL 0x0255000b, \fail_target
	EXPECT_ILLEGAL 0x0265000b, \fail_target
	EXPECT_ILLEGAL 0x0215000b, \fail_target
	EXPECT_ILLEGAL 0x0235000b, \fail_target
	EXPECT_ILLEGAL 0x0225000b, \fail_target
	EXPECT_ILLEGAL 0x0285000b, \fail_target
	EXPECT_ILLEGAL 0x0100000b, \fail_target
	EXPECT_ILLEGAL 0x0110000b, \fail_target
	EXPECT_ILLEGAL 0x0385000b, \fail_target
	EXPECT_ILLEGAL 0x0150000b, \fail_target
	EXPECT_ILLEGAL 0x0170000b, \fail_target
	EXPECT_ILLEGAL 0x0160000b, \fail_target
	.endm

	.macro	CMO_ALL_ILLEGAL fail_target
	EXPECT_ILLEGAL 0x0010000b, \fail_target
	EXPECT_ILLEGAL 0x0030000b, \fail_target
	EXPECT_ILLEGAL 0x0020000b, \fail_target
	EXPECT_ILLEGAL 0x0295000b, \fail_target
	EXPECT_ILLEGAL 0x02b5000b, \fail_target
	EXPECT_ILLEGAL 0x02a5000b, \fail_target
	EXPECT_ILLEGAL 0x0255000b, \fail_target
	EXPECT_ILLEGAL 0x0275000b, \fail_target
	EXPECT_ILLEGAL 0x0265000b, \fail_target
	EXPECT_ILLEGAL 0x0215000b, \fail_target
	EXPECT_ILLEGAL 0x0235000b, \fail_target
	EXPECT_ILLEGAL 0x0225000b, \fail_target
	EXPECT_ILLEGAL 0x0285000b, \fail_target
	EXPECT_ILLEGAL 0x0245000b, \fail_target
	EXPECT_ILLEGAL 0x0100000b, \fail_target
	EXPECT_ILLEGAL 0x0110000b, \fail_target
	EXPECT_ILLEGAL 0x0385000b, \fail_target
	EXPECT_ILLEGAL 0x0305000b, \fail_target
	EXPECT_ILLEGAL 0x0150000b, \fail_target
	EXPECT_ILLEGAL 0x0170000b, \fail_target
	EXPECT_ILLEGAL 0x0160000b, \fail_target
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
	li	s1, 0
	li	s5, 0

	li	s3, 1
	csrr	t0, CSR_TH_MXSTATUS
	li	t1, MXSTATUS_THEADISAEE
	and	t2, t0, t1
	bne	t2, t1, fail

#ifdef XTHEADBA_DISABLED
	/* A sibling XThead extension keeps the shared decoder installed. */
	li	s3, 2
	CHECK_ADDSL_ILLEGAL fail
	li	a0, 7
	li	a1, 11
	add	a2, a0, a1
	li	t0, 18
	bne	a2, t0, fail
	lla	a0, cmo_operand
	.word	0x0275000b	/* XTheadCmo remains enabled. */
	li	t0, 4
	bne	s0, t0, fail
	j	pass
#else
	li	t1, MXSTATUS_UCME
	and	t2, t0, t1
	bne	t2, t1, fail

	/* Reset state: all scalar forms are legal in M mode. */
	li	s3, 2
	CHECK_ADDSL fail
	lla	a0, cmo_operand
	CMO_ALL_LEGAL

	/* THEADISAEE=1, UCME=1: all forms are legal in S mode. */
	li	s3, 3
	lla	s4, after_s_enabled
	lla	t0, s_enabled
	csrw	mepc, t0
	SET_MPP MSTATUS_MPP_S
	mret
after_s_enabled:

	/* U mode admits addsl and exactly three documented CMO forms. */
	li	s3, 4
	lla	s4, after_u_enabled
	lla	t0, u_enabled
	csrw	mepc, t0
	SET_MPP MSTATUS_MPP_U
	mret
after_u_enabled:

	/* UCME does not affect M/S, but disables every CMO in U mode. */
	li	s3, 5
	li	t0, MXSTATUS_UCME
	csrc	CSR_TH_MXSTATUS, t0
	csrr	t1, CSR_TH_MXSTATUS
	and	t1, t1, t0
	bnez	t1, fail
	lla	a0, cmo_operand
	CMO_ALL_LEGAL

	li	s3, 6
	lla	s4, after_s_ucme_off
	lla	t0, s_ucme_off
	csrw	mepc, t0
	SET_MPP MSTATUS_MPP_S
	mret
after_s_ucme_off:

	li	s3, 7
	lla	s4, after_u_ucme_off
	lla	t0, u_ucme_off
	csrw	mepc, t0
	SET_MPP MSTATUS_MPP_U
	mret
after_u_ucme_off:

	li	t0, MXSTATUS_UCME
	csrs	CSR_TH_MXSTATUS, t0
	csrr	t1, CSR_TH_MXSTATUS
	and	t1, t1, t0
	bne	t1, t0, fail

	/* THEADISAEE gates all four addsl and all 21 CMO encodings. */
	li	s3, 8
	li	t0, MXSTATUS_THEADISAEE
	csrc	CSR_TH_MXSTATUS, t0
	csrr	t1, CSR_TH_MXSTATUS
	and	t1, t1, t0
	bnez	t1, fail
	CHECK_ADDSL_ILLEGAL fail
	lla	a0, cmo_operand
	CMO_ALL_ILLEGAL fail

	/* Re-enabling must select the enabled translation state again. */
	li	s3, 9
	li	t0, MXSTATUS_THEADISAEE
	csrs	CSR_TH_MXSTATUS, t0
	CHECK_ADDSL fail
	lla	a0, cmo_operand
	CMO_ALL_LEGAL

	li	t0, 64
	bne	s0, t0, fail
	j	pass

s_enabled:
	CHECK_ADDSL lower_fail
	lla	a0, cmo_operand
	CMO_ALL_LEGAL
	li	s5, 1
	ecall
	j	.

u_enabled:
	CHECK_ADDSL lower_fail
	lla	a0, cmo_operand
	CMO_U_MS_ONLY lower_fail
	CMO_U_LEGAL
	li	s5, 1
	ecall
	j	.

s_ucme_off:
	lla	a0, cmo_operand
	CMO_ALL_LEGAL
	li	s5, 1
	ecall
	j	.

u_ucme_off:
	lla	a0, cmo_operand
	CMO_ALL_ILLEGAL lower_fail
	li	s5, 1
	ecall
	j	.

lower_fail:
	li	s5, 2
	ecall
	j	.
#endif

pass:
	li	a0, 0
	j	exit

	.balign	4
mtrap:
	csrr	t0, mcause
	beq	t0, s1, expected_trap
	li	t1, CAUSE_ECALL_U
	beq	t0, t1, lower_return
	li	t1, CAUSE_ECALL_S
	beq	t0, t1, lower_return
	li	s3, 90
	j	fail

expected_trap:
	beqz	s1, wrong_cause
	csrr	t0, mepc
	bne	t0, s2, wrong_pc
	csrr	t0, mtval
	bne	t0, s7, wrong_tval
	li	s1, 0
	addi	s0, s0, 1
	csrr	t0, mepc
	addi	t0, t0, 4
	csrw	mepc, t0
	mret

lower_return:
	li	t0, 1
	bne	s5, t0, lower_reported_failure
	li	s5, 0
	csrw	mepc, s4
	SET_MPP MSTATUS_MPP_M
	mret

wrong_cause:
	li	s3, 91
	j	fail
wrong_pc:
	li	s3, 92
	j	fail
wrong_tval:
	li	s3, 93
	j	fail
lower_reported_failure:
	/* s3 still identifies the lower-mode phase that failed. */
	j	fail

fail:
	mv	a0, s3
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
	.balign	64
cmo_operand:
	.space	64

	.data
	.balign	16
semiargs:
	.space	16
