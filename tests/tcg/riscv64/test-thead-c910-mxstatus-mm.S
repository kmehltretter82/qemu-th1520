/*
 * T-Head C910 MXSTATUS.MM misaligned-access test.
 *
 * openC910 routes MXSTATUS.MM to the LSU.  Regular scalar integer and
 * floating-point accesses may be misaligned when MM is set; they trap when
 * MM is clear.  Atomic and vector accesses remain naturally aligned.
 *
 * The test also covers all scalar memory translation paths used by QEMU's
 * C910 model: standard integer/FP, XTheadMemIdx incrementing and indexed,
 * XTheadMemPair, and XTheadFMemIdx instructions.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.option	norelax
	.option	norvc

#define CSR_TH_MXSTATUS		0x7c0
#define CSR_TH_SXSTATUS		0x5c0
#define MXSTATUS_MM		(1 << 15)
#define MSTATUS_FS_INITIAL	(1 << 13)
#define MSTATUS_VS_THEAD	(1 << 23)
#define CAUSE_LOAD_MISALIGNED	4
#define CAUSE_STORE_MISALIGNED	6

/*
 * Persistent registers:
 *   s0 = read-only input buffer
 *   s1 = expected mcause
 *   s2 = expected mtval
 *   s3 = trap counter
 *   s4 = trap-counter snapshot
 *   s5 = writable scratch buffer
 *   s6 = failure stage
 */
	.macro	EXPECT cause, base, off
	li	s1, \cause
	addi	s2, \base, \off
	mv	s4, s3
	.endm

	.macro	CHECK_NO_TRAP
	bne	s3, s4, fail
	.endm

	.macro	CHECK_TRAP
	addi	s4, s4, 1
	bne	s3, s4, fail
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
	lla	t0, trap
	csrw	mtvec, t0
	lla	s0, input
	lla	s5, scratch
	li	s3, 0

	/* Enable floating-point and the C910 XTheadVector state. */
	li	t0, MSTATUS_FS_INITIAL | MSTATUS_VS_THEAD
	csrs	mstatus, t0

	/* MM is set by the C910 reset state. */
	li	s6, 1
	csrr	t0, CSR_TH_MXSTATUS
	li	t1, MXSTATUS_MM
	and	t0, t0, t1
	bne	t0, t1, fail

	/* Standard integer load/store: reset MM permits misalignment. */
	li	s6, 2
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	lw	a2, 1(s0)
	CHECK_NO_TRAP
	li	t0, 0x44332211
	bne	a2, t0, fail

	li	t6, 0x1122334455667788
	EXPECT	CAUSE_STORE_MISALIGNED, s5, 1
	sw	t6, 1(s5)
	CHECK_NO_TRAP
	lbu	t0, 1(s5)
	li	t1, 0x88
	bne	t0, t1, fail

	/* Standard floating-point load/store. */
	li	s6, 3
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	fld	fa0, 1(s0)
	CHECK_NO_TRAP
	EXPECT	CAUSE_STORE_MISALIGNED, s5, 1
	fsd	fa0, 1(s5)
	CHECK_NO_TRAP
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	flh	fa1, 1(s0)
	CHECK_NO_TRAP
	EXPECT	CAUSE_STORE_MISALIGNED, s5, 1
	fsh	fa1, 1(s5)
	CHECK_NO_TRAP

	/* XTheadMemIdx indexed load/store. */
	li	s6, 4
	li	t0, 1
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	th.lrw	a2, s0, t0, 0
	CHECK_NO_TRAP
	li	t1, 0x44332211
	bne	a2, t1, fail
	li	t0, 1
	EXPECT	CAUSE_STORE_MISALIGNED, s5, 1
	th.srw	t6, s5, t0, 0
	CHECK_NO_TRAP

	/* XTheadMemIdx post-increment load/store. */
	li	s6, 5
	addi	t4, s0, 1
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	th.lwia	a2, (t4), 1, 0
	CHECK_NO_TRAP
	addi	t5, s0, 2
	bne	t4, t5, fail
	addi	t4, s5, 1
	EXPECT	CAUSE_STORE_MISALIGNED, s5, 1
	th.swia	t6, (t4), 1, 0
	CHECK_NO_TRAP
	addi	t5, s5, 2
	bne	t4, t5, fail

	/* XTheadMemPair load/store. */
	li	s6, 6
	addi	t4, s0, 1
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	th.lwud	a2, a3, (t4), 0, 3
	CHECK_NO_TRAP
	li	t0, 0x44332211
	bne	a2, t0, fail
	li	t0, 0x88776655
	bne	a3, t0, fail
	addi	t4, s5, 1
	EXPECT	CAUSE_STORE_MISALIGNED, s5, 1
	th.swd	t5, t6, (t4), 0, 3
	CHECK_NO_TRAP

	/* XTheadFMemIdx load/store.  The encoded rd field names an FPR. */
	li	s6, 7
	li	t0, 1
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	th.flrw	fa0, s0, t0, 0
	CHECK_NO_TRAP
	li	t0, 1
	EXPECT	CAUSE_STORE_MISALIGNED, s5, 1
	th.fsrw	fa0, s5, t0, 0
	CHECK_NO_TRAP

	/* Clearing MM must affect the first following memory instruction. */
	li	s6, 8
	li	t0, MXSTATUS_MM
	csrc	CSR_TH_MXSTATUS, t0
	csrr	t1, CSR_TH_MXSTATUS
	and	t1, t1, t0
	bnez	t1, fail

	/* Standard integer and FP paths now trap. */
	li	s6, 9
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	lw	a2, 1(s0)
	CHECK_TRAP
	EXPECT	CAUSE_STORE_MISALIGNED, s5, 1
	sw	t6, 1(s5)
	CHECK_TRAP
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	flw	fa0, 1(s0)
	CHECK_TRAP
	EXPECT	CAUSE_STORE_MISALIGNED, s5, 1
	fsw	fa0, 1(s5)
	CHECK_TRAP
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	flh	fa1, 1(s0)
	CHECK_TRAP
	EXPECT	CAUSE_STORE_MISALIGNED, s5, 1
	fsh	fa1, 1(s5)
	CHECK_TRAP

	/* Indexed XTheadMemIdx paths now trap. */
	li	s6, 10
	li	t0, 1
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	th.lrw	a2, s0, t0, 0
	CHECK_TRAP
	li	t0, 1
	EXPECT	CAUSE_STORE_MISALIGNED, s5, 1
	th.srw	t6, s5, t0, 0
	CHECK_TRAP

	/* Incrementing forms trap before their architectural base update. */
	li	s6, 11
	addi	t4, s0, 1
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	th.lwia	a2, (t4), 1, 0
	CHECK_TRAP
	addi	t5, s0, 1
	bne	t4, t5, fail
	addi	t4, s5, 1
	EXPECT	CAUSE_STORE_MISALIGNED, s5, 1
	th.swia	t6, (t4), 1, 0
	CHECK_TRAP
	addi	t5, s5, 1
	bne	t4, t5, fail

	/* Pair and FP-indexed paths now trap. */
	li	s6, 12
	addi	t4, s0, 1
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	th.lwud	a2, a3, (t4), 0, 3
	CHECK_TRAP
	addi	t4, s5, 1
	EXPECT	CAUSE_STORE_MISALIGNED, s5, 1
	th.swd	t5, t6, (t4), 0, 3
	CHECK_TRAP
	li	t0, 1
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	th.flrw	fa0, s0, t0, 0
	CHECK_TRAP
	li	t0, 1
	EXPECT	CAUSE_STORE_MISALIGNED, s5, 1
	th.fsrw	fa0, s5, t0, 0
	CHECK_TRAP

	/* Re-enabling MM restores scalar behavior without enabling Zicclsm. */
	li	s6, 13
	li	t0, MXSTATUS_MM
	csrs	CSR_TH_MXSTATUS, t0
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	ld	a2, 1(s0)
	CHECK_NO_TRAP
	li	t0, 0x8877665544332211
	bne	a2, t0, fail
	li	t0, 1
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	th.lrw	a2, s0, t0, 0
	CHECK_NO_TRAP

	/* SXSTATUS.MM aliases MXSTATUS.MM and must also select new TBs. */
	li	s6, 14
	li	t0, MXSTATUS_MM
	csrc	CSR_TH_SXSTATUS, t0
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	lw	a2, 1(s0)
	CHECK_TRAP
	li	t0, MXSTATUS_MM
	csrs	CSR_TH_SXSTATUS, t0
	csrr	t1, CSR_TH_MXSTATUS
	and	t1, t1, t0
	bne	t1, t0, fail
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	lw	a2, 1(s0)
	CHECK_NO_TRAP

	/* Atomic accesses remain aligned-only while MM is set. */
	li	s6, 15
	addi	t4, s0, 1
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	lr.w	t5, (t4)
	CHECK_TRAP
	EXPECT	CAUSE_STORE_MISALIGNED, s5, 1
	addi	t4, s5, 1
	amoswap.w	t5, t6, (t4)
	CHECK_TRAP

	/* XTheadVector accesses also remain aligned-only while MM is set. */
	li	s6, 16
	li	t1, 1
	.word	0x008372d7	/* th.vsetvli t0, t1, e32, m1 */
	li	s6, 17
	addi	t2, s0, 1
	EXPECT	CAUSE_LOAD_MISALIGNED, s0, 1
	.word	0x1203e087	/* th.vlw.v v1, (t2) */
	CHECK_TRAP
	li	s6, 18
	addi	t2, s5, 1
	EXPECT	CAUSE_STORE_MISALIGNED, s5, 1
	.word	0x0203e0a7	/* th.vsw.v v1, (t2) */
	CHECK_TRAP

	li	a0, 0
	j	exit

	.balign	4
trap:
	csrr	t0, mcause
	beq	t0, s1, 1f
	li	s6, 90
	j	fail
1:
	csrr	t1, mtval
	beq	t1, s2, 2f
	li	s6, 91
	j	fail
2:
	addi	s3, s3, 1
	csrr	t0, mepc
	addi	t0, t0, 4
	csrw	mepc, t0
	mret

fail:
	mv	a0, s6
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
input:
	.byte	0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77
	.byte	0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
	.space	48

	.data
	.balign	64
scratch:
	.space	64
semiargs:
	.space	16
