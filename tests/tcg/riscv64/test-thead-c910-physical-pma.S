/*
 * T-Head C910 physical PMA fallback test.
 *
 * This payload is paired with an explicitly synthetic eight-region table.
 * It verifies that MMU-off and Bare accesses always use the physical table,
 * that Sv39 uses it while MXSTATUS.MAEE is clear, and that PTE[63:59] takes
 * precedence while MAEE is set.  The table alternates normal, strong-order
 * and non-cacheable one-page regions so every upper boundary and the default
 * are exercised without claiming that these are TH1520 silicon values.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.option	norelax
	.option	norvc

#define CSR_TH_MXSTATUS		0x7c0
#define MXSTATUS_MM		(1 << 15)
#define MXSTATUS_MAEE		(1 << 21)

#define MSTATUS_MPP_MASK	(3 << 11)
#define MSTATUS_MPP_S		(1 << 11)
#define MSTATUS_MPP_M		(3 << 11)
#define MSTATUS_VS_THEAD	(1 << 23)

#define PTE_V			0x001
#define PTE_S_RWX_AD		0x0cf
#define PTE_U_RWX_AD		0x0df
#define MAEE_NORMAL		(0xe << 59)
#define MAEE_STRONG		(0x12 << 59)

#define SATP_SV39		(8 << 60)
#define S_NORMAL_VA		0x40000000
#define S_STRONG_VA		0x40001000
#define S_NONCACHE_VA		0x40002000
#define S_DEFAULT_VA		0x40003000
#define U_NORMAL_VA		0xc0000000
#define U_STRONG_VA		0xc0001000
#define U_NONCACHE_VA		0xc0002000
#define U_DEFAULT_VA		0xc0003000
#define USER_CODE_DELTA		0x80000000

#define PRV_U			0
#define CAUSE_INST_ACCESS	1
#define CAUSE_LOAD_MISALIGNED	4
#define CAUSE_LOAD_ACCESS	5
#define CAUSE_STORE_ACCESS	7
#define CAUSE_ECALL_U		8
#define CAUSE_ECALL_S		9
#define CAUSE_ECALL_M		11
#define DELEGATED_EXCEPTIONS	0xf2

/*
 * Persistent registers:
 *   s0  = current test address
 *   s1  = expected cause
 *   s2  = expected trap value
 *   s3  = trap counter
 *   s4  = trap-counter snapshot
 *   s5  = instruction-fault recovery PC, or zero
 *   s6  = failure stage
 *   s7  = successful lower-mode return PC
 *   s8  = expected handler (0 = M, 1 = S)
 *   s9  = lower-mode control ecall (1 = success, 2 = failure)
 *   s11 = failure return PC
 */
	.macro	EXPECT handler, cause, value
	li	s8, \handler
	li	s1, \cause
	mv	s2, \value
	mv	s4, s3
	li	s5, 0
	.endm

	.macro	EXPECT_INSN handler, target, recovery
	li	s8, \handler
	li	s1, CAUSE_INST_ACCESS
	mv	s2, \target
	mv	s4, s3
	lla	s5, \recovery
	.endm

	.macro	CHECK_NO_TRAP
	bne	s3, s4, lower_fail
	.endm

	.macro	CHECK_TRAP
	addi	s4, s4, 1
	bne	s3, s4, lower_fail
	.endm

	.macro	SET_MPP value
	csrr	t0, mstatus
	li	t1, MSTATUS_MPP_MASK
	not	t1, t1
	and	t0, t0, t1
	li	t1, \value
	or	t0, t0, t1
	csrw	mstatus, t0
	.endm

	.macro	INIT_PAGE page
	lla	t0, \page
	li	t1, 0x02a00513	/* addi a0, zero, 42 */
	sw	t1, 0(t0)
	li	t1, 0x00008067	/* ret */
	sw	t1, 4(t0)
	li	t1, 0x8877665544332211
	sd	t1, 16(t0)
	li	t1, 5
	sw	t1, 32(t0)
	li	t1, 0x12345678
	sw	t1, 48(t0)
	.endm

	.macro	MAP_PAGE table, offset, page, flags
	lla	t0, \table
	lla	t1, \page
	srli	t1, t1, 2
	li	t2, \flags
	or	t1, t1, t2
	sd	t1, \offset(t0)
	.endm

	.macro	TEST_NORMAL_AMO page, stage
	li	s6, \stage
	lla	t4, \page
	addi	t4, t4, 32
	li	t6, 1
	mv	s4, s3
	amoadd.w a2, t6, (t4)
	CHECK_NO_TRAP
	li	t0, 5
	bne	a2, t0, lower_fail
	.endm

	.macro	TEST_NONCACHE_AMO page, stage
	li	s6, \stage
	lla	t4, \page
	addi	t4, t4, 32
	li	t6, 1
	EXPECT	0, CAUSE_STORE_ACCESS, zero
	amoadd.w a2, t6, (t4)
	CHECK_TRAP
	.endm

	.macro	TEST_STRONG_DIRECT page, stage
	li	s6, \stage
	lla	s0, \page
	addi	t4, s0, 17
	EXPECT	0, CAUSE_LOAD_MISALIGNED, t4
	lwu	a2, 0(t4)
	CHECK_TRAP

	li	t1, 1
	.word	0x008372d7	/* th.vsetvli t0, t1, e32, m1 */
	addi	t2, s0, 48
	EXPECT	0, CAUSE_LOAD_ACCESS, zero
	.word	0x1203e087	/* th.vlw.v v1, (t2) */
	CHECK_TRAP

	mv	t4, s0
	EXPECT_INSN 0, t4, .Lstrong_recovery\@
	jalr	ra, t4, 0
	j	lower_fail
.Lstrong_recovery\@:
	CHECK_TRAP
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
	lla	t0, strap
	csrw	stvec, t0
	csrw	medeleg, zero
	li	s3, 0
	li	s9, 0
	lla	s11, fail

	li	t0, MSTATUS_VS_THEAD
	csrs	mstatus, t0
	li	t0, MXSTATUS_MM | MXSTATUS_MAEE
	csrs	CSR_TH_MXSTATUS, t0

	INIT_PAGE pma_normal0
	INIT_PAGE pma_strong0
	INIT_PAGE pma_noncache0
	INIT_PAGE pma_normal1
	INIT_PAGE pma_strong1
	INIT_PAGE pma_noncache1
	INIT_PAGE pma_normal2
	INIT_PAGE pma_strong2
	INIT_PAGE pma_default

	/* M-mode direct accesses cover all eight regions and the default. */
	TEST_NORMAL_AMO pma_normal0, 1
	TEST_STRONG_DIRECT pma_strong0, 2
	TEST_NONCACHE_AMO pma_noncache0, 3
	TEST_NORMAL_AMO pma_normal1, 4
	TEST_STRONG_DIRECT pma_strong1, 5
	TEST_NONCACHE_AMO pma_noncache1, 6
	TEST_NORMAL_AMO pma_normal2, 7
	TEST_STRONG_DIRECT pma_strong2, 8
	TEST_NONCACHE_AMO pma_default, 9

	/* Bare S mode uses the physical table even though MAEE remains set. */
	li	s6, 10
	lla	s7, after_s_bare_on
	lla	t0, s_mode_bare
	csrw	mepc, t0
	SET_MPP MSTATUS_MPP_S
	mret

after_s_bare_on:
	/* Build S/U aliases with deliberately contrary PTE attributes. */
	lla	t0, root_pt
	lla	t1, page_tables_end
1:
	sd	zero, 0(t0)
	addi	t0, t0, 8
	bltu	t0, t1, 1b

	lla	t0, root_pt
	lla	t1, s_l1
	srli	t1, t1, 2
	ori	t1, t1, PTE_V
	sd	t1, 8(t0)		/* root[1]: S data aliases */
	lla	t1, u_l1
	srli	t1, t1, 2
	ori	t1, t1, PTE_V
	sd	t1, 24(t0)		/* root[3]: U data aliases */

	li	t1, 0x80000000
	srli	t1, t1, 2
	li	t2, PTE_S_RWX_AD | MAEE_NORMAL
	or	t1, t1, t2
	sd	t1, 16(t0)		/* root[2]: S identity map */
	li	t1, 0x80000000
	srli	t1, t1, 2
	li	t2, PTE_U_RWX_AD | MAEE_NORMAL
	or	t1, t1, t2
	sd	t1, 32(t0)		/* root[4]: U code alias */

	lla	t0, s_l1
	lla	t1, s_l0
	srli	t1, t1, 2
	ori	t1, t1, PTE_V
	sd	t1, 0(t0)
	lla	t0, u_l1
	lla	t1, u_l0
	srli	t1, t1, 2
	ori	t1, t1, PTE_V
	sd	t1, 0(t0)

	MAP_PAGE s_l0, 0, pma_normal0, PTE_S_RWX_AD | MAEE_STRONG
	MAP_PAGE s_l0, 8, pma_strong0, PTE_S_RWX_AD | MAEE_NORMAL
	MAP_PAGE s_l0, 16, pma_noncache0, PTE_S_RWX_AD | MAEE_NORMAL
	MAP_PAGE s_l0, 24, pma_default, PTE_S_RWX_AD | MAEE_NORMAL
	MAP_PAGE u_l0, 0, pma_normal0, PTE_U_RWX_AD | MAEE_STRONG
	MAP_PAGE u_l0, 8, pma_strong0, PTE_U_RWX_AD | MAEE_NORMAL
	MAP_PAGE u_l0, 16, pma_noncache0, PTE_U_RWX_AD | MAEE_NORMAL
	MAP_PAGE u_l0, 24, pma_default, PTE_U_RWX_AD | MAEE_NORMAL

	lla	t0, root_pt
	srli	t0, t0, 12
	li	t1, SATP_SV39
	or	t0, t0, t1
	csrw	satp, t0
	sfence.vma

	/* With MAEE set, PTE attributes override contrary physical regions. */
	li	s6, 20
	lla	s7, after_s_maee_on
	lla	t0, s_mode_maee_on
	csrw	mepc, t0
	SET_MPP MSTATUS_MPP_S
	mret

after_s_maee_on:
	li	t0, DELEGATED_EXCEPTIONS
	csrw	medeleg, t0
	li	s6, 30
	lla	s7, after_u_maee_on
	lla	t0, u_mode_maee_on
	li	t1, USER_CODE_DELTA
	add	t0, t0, t1
	csrw	mepc, t0
	SET_MPP PRV_U
	mret

after_u_maee_on:
	csrw	medeleg, zero
	li	t0, MXSTATUS_MAEE
	csrc	CSR_TH_MXSTATUS, t0
	sfence.vma

	/* M-mode direct lookup is independent of the MAEE enable bit. */
	li	s6, 40
	lla	s0, pma_strong1
	addi	t4, s0, 17
	EXPECT	0, CAUSE_LOAD_MISALIGNED, t4
	lwu	a2, 0(t4)
	CHECK_TRAP
	TEST_NONCACHE_AMO pma_noncache1, 41

	/* Repeat the Bare path with MAEE clear. */
	csrw	satp, zero
	sfence.vma
	li	s6, 42
	lla	s7, after_s_bare_off
	lla	t0, s_mode_bare
	csrw	mepc, t0
	SET_MPP MSTATUS_MPP_S
	mret

after_s_bare_off:
	lla	t0, root_pt
	srli	t0, t0, 12
	li	t1, SATP_SV39
	or	t0, t0, t1
	csrw	satp, t0
	sfence.vma

	/* Both translated privilege modes use physical PMAs with MAEE clear. */
	li	s6, 50
	lla	s7, after_s_maee_off
	lla	t0, s_mode_maee_off
	csrw	mepc, t0
	SET_MPP MSTATUS_MPP_S
	mret

after_s_maee_off:
	li	t0, DELEGATED_EXCEPTIONS
	csrw	medeleg, t0
	li	s6, 60
	lla	s7, after_u_maee_off
	lla	t0, u_mode_maee_off
	li	t1, USER_CODE_DELTA
	add	t0, t0, t1
	csrw	mepc, t0
	SET_MPP PRV_U
	mret

after_u_maee_off:
	li	s6, 70
	li	t0, 38
	bne	s3, t0, fail
	li	a0, 0
	j	exit

/* Bare tests use physical addresses and always trap to M. */
s_mode_bare:
	li	s6, 11
	lla	s0, pma_strong0
	addi	t4, s0, 17
	EXPECT	0, CAUSE_LOAD_MISALIGNED, t4
	lwu	a2, 0(t4)
	CHECK_TRAP

	li	s6, 12
	lla	t4, pma_noncache0
	addi	t4, t4, 32
	li	t6, 1
	EXPECT	0, CAUSE_STORE_ACCESS, zero
	amoadd.w a2, t6, (t4)
	CHECK_TRAP

	li	s6, 13
	lla	t4, pma_normal0
	addi	t4, t4, 32
	li	t6, 5
	sw	t6, 0(t4)
	li	t6, 1
	mv	s4, s3
	amoadd.w a2, t6, (t4)
	CHECK_NO_TRAP
	li	s9, 1
	ecall
	j	lower_fail

/* PTE strong on physical normal; PTE normal on all other aliases. */
s_mode_maee_on:
	li	s6, 21
	li	s0, S_NORMAL_VA
	addi	t4, s0, 17
	EXPECT	0, CAUSE_LOAD_MISALIGNED, t4
	lwu	a2, 0(t4)
	CHECK_TRAP
	li	t1, 1
	.word	0x008372d7	/* th.vsetvli t0, t1, e32, m1 */
	addi	t2, s0, 48
	EXPECT	0, CAUSE_LOAD_ACCESS, zero
	.word	0x1203e087	/* th.vlw.v v1, (t2) */
	CHECK_TRAP
	mv	t4, s0
	EXPECT_INSN 0, t4, s_on_after_fetch
	jalr	ra, t4, 0
	j	lower_fail
s_on_after_fetch:
	CHECK_TRAP
	addi	t4, s0, 32
	li	t6, 1
	EXPECT	0, CAUSE_STORE_ACCESS, zero
	amoadd.w a2, t6, (t4)
	CHECK_TRAP

	li	s6, 22
	li	s0, S_STRONG_VA
	addi	t4, s0, 17
	mv	s4, s3
	lwu	a2, 0(t4)
	CHECK_NO_TRAP
	li	t0, 0x55443322
	bne	a2, t0, lower_fail
	li	t1, 1
	.word	0x008372d7
	addi	t2, s0, 48
	.word	0x1203e087
	mv	t4, s0
	jalr	ra, t4, 0
	li	t0, 42
	bne	a0, t0, lower_fail
	addi	t4, s0, 32
	li	t6, 5
	sw	t6, 0(t4)
	li	t6, 1
	amoadd.w a2, t6, (t4)

	li	s0, S_NONCACHE_VA
	addi	t4, s0, 32
	li	t6, 1
	amoadd.w a2, t6, (t4)
	li	s0, S_DEFAULT_VA
	addi	t4, s0, 32
	amoadd.w a2, t6, (t4)
	CHECK_NO_TRAP
	li	s9, 1
	ecall
	j	lower_fail

u_mode_maee_on:
	li	s6, 31
	li	s0, U_NORMAL_VA
	addi	t4, s0, 17
	EXPECT	1, CAUSE_LOAD_MISALIGNED, t4
	lwu	a2, 0(t4)
	CHECK_TRAP
	li	t1, 1
	.word	0x008372d7
	addi	t2, s0, 48
	EXPECT	1, CAUSE_LOAD_ACCESS, zero
	.word	0x1203e087
	CHECK_TRAP
	mv	t4, s0
	EXPECT_INSN 1, t4, u_on_after_fetch
	jalr	ra, t4, 0
	j	lower_fail
u_on_after_fetch:
	CHECK_TRAP
	addi	t4, s0, 32
	li	t6, 1
	EXPECT	1, CAUSE_STORE_ACCESS, zero
	amoadd.w a2, t6, (t4)
	CHECK_TRAP

	li	s6, 32
	li	s0, U_STRONG_VA
	addi	t4, s0, 17
	mv	s4, s3
	lwu	a2, 0(t4)
	CHECK_NO_TRAP
	li	t1, 1
	.word	0x008372d7
	addi	t2, s0, 48
	.word	0x1203e087
	mv	t4, s0
	jalr	ra, t4, 0
	li	t0, 42
	bne	a0, t0, lower_fail
	addi	t4, s0, 32
	li	t6, 1
	amoadd.w a2, t6, (t4)
	li	s0, U_NONCACHE_VA
	addi	t4, s0, 32
	amoadd.w a2, t6, (t4)
	li	s0, U_DEFAULT_VA
	addi	t4, s0, 32
	amoadd.w a2, t6, (t4)
	CHECK_NO_TRAP
	li	s9, 1
	ecall
	j	lower_fail

/* Physical attributes override every contrary PTE while MAEE is clear. */
s_mode_maee_off:
	li	s6, 51
	li	s0, S_NORMAL_VA
	addi	t4, s0, 17
	mv	s4, s3
	lwu	a2, 0(t4)
	CHECK_NO_TRAP
	li	t1, 1
	.word	0x008372d7
	addi	t2, s0, 48
	.word	0x1203e087
	mv	t4, s0
	jalr	ra, t4, 0
	li	t0, 42
	bne	a0, t0, lower_fail
	addi	t4, s0, 32
	li	t6, 1
	amoadd.w a2, t6, (t4)
	CHECK_NO_TRAP

	li	s6, 52
	li	s0, S_STRONG_VA
	addi	t4, s0, 17
	EXPECT	0, CAUSE_LOAD_MISALIGNED, t4
	lwu	a2, 0(t4)
	CHECK_TRAP
	li	t1, 1
	.word	0x008372d7
	addi	t2, s0, 48
	EXPECT	0, CAUSE_LOAD_ACCESS, zero
	.word	0x1203e087
	CHECK_TRAP
	mv	t4, s0
	EXPECT_INSN 0, t4, s_off_after_fetch
	jalr	ra, t4, 0
	j	lower_fail
s_off_after_fetch:
	CHECK_TRAP
	addi	t4, s0, 32
	li	t6, 1
	EXPECT	0, CAUSE_STORE_ACCESS, zero
	amoadd.w a2, t6, (t4)
	CHECK_TRAP

	li	s6, 53
	li	s0, S_NONCACHE_VA
	addi	t4, s0, 32
	EXPECT	0, CAUSE_STORE_ACCESS, zero
	amoadd.w a2, t6, (t4)
	CHECK_TRAP
	lr.w	t5, (t4)
	sc.w	t6, t5, (t4)
	bnez	t6, lower_fail
	mv	t4, s0
	jalr	ra, t4, 0
	li	t0, 42
	bne	a0, t0, lower_fail

	li	s0, S_DEFAULT_VA
	addi	t4, s0, 32
	li	t6, 1
	EXPECT	0, CAUSE_STORE_ACCESS, zero
	amoadd.w a2, t6, (t4)
	CHECK_TRAP
	li	s9, 1
	ecall
	j	lower_fail

u_mode_maee_off:
	li	s6, 61
	li	s0, U_NORMAL_VA
	addi	t4, s0, 17
	mv	s4, s3
	lwu	a2, 0(t4)
	CHECK_NO_TRAP
	li	t1, 1
	.word	0x008372d7
	addi	t2, s0, 48
	.word	0x1203e087
	mv	t4, s0
	jalr	ra, t4, 0
	li	t0, 42
	bne	a0, t0, lower_fail
	addi	t4, s0, 32
	li	t6, 1
	amoadd.w a2, t6, (t4)
	CHECK_NO_TRAP

	li	s6, 62
	li	s0, U_STRONG_VA
	addi	t4, s0, 17
	EXPECT	1, CAUSE_LOAD_MISALIGNED, t4
	lwu	a2, 0(t4)
	CHECK_TRAP
	li	t1, 1
	.word	0x008372d7
	addi	t2, s0, 48
	EXPECT	1, CAUSE_LOAD_ACCESS, zero
	.word	0x1203e087
	CHECK_TRAP
	mv	t4, s0
	EXPECT_INSN 1, t4, u_off_after_fetch
	jalr	ra, t4, 0
	j	lower_fail
u_off_after_fetch:
	CHECK_TRAP
	addi	t4, s0, 32
	li	t6, 1
	EXPECT	1, CAUSE_STORE_ACCESS, zero
	amoadd.w a2, t6, (t4)
	CHECK_TRAP

	li	s6, 63
	li	s0, U_NONCACHE_VA
	addi	t4, s0, 32
	EXPECT	1, CAUSE_STORE_ACCESS, zero
	amoadd.w a2, t6, (t4)
	CHECK_TRAP
	lr.w	t5, (t4)
	sc.w	t6, t5, (t4)
	bnez	t6, lower_fail
	li	s0, U_DEFAULT_VA
	addi	t4, s0, 32
	li	t6, 1
	EXPECT	1, CAUSE_STORE_ACCESS, zero
	amoadd.w a2, t6, (t4)
	CHECK_TRAP
	li	s9, 1
	ecall
	j	lower_fail

	.balign	4
mtrap:
	bnez	s9, lower_control_trap
	bnez	s8, wrong_m_handler
	csrr	t0, mcause
	bne	t0, s1, wrong_m_cause
	csrr	t1, mtval
	bne	t1, s2, wrong_m_tval
	addi	s3, s3, 1
	bnez	s5, 1f
	csrr	t0, mepc
	addi	t0, t0, 4
	j	2f
1:
	mv	t0, s5
	li	s5, 0
2:
	csrw	mepc, t0
	mret

lower_control_trap:
	csrr	t0, mcause
	li	t1, CAUSE_ECALL_U
	beq	t0, t1, 1f
	li	t1, CAUSE_ECALL_S
	beq	t0, t1, 1f
	li	t1, CAUSE_ECALL_M
	bne	t0, t1, wrong_control_cause
1:
	li	t0, 2
	beq	s9, t0, 2f
	mv	t0, s7
	j	3f
2:
	mv	t0, s11
3:
	csrw	mepc, t0
	SET_MPP MSTATUS_MPP_M
	li	s9, 0
	mret

wrong_m_handler:
	li	s6, 90
	j	fail
wrong_m_cause:
	li	s6, 91
	j	fail
wrong_m_tval:
	li	s6, 92
	j	fail
wrong_control_cause:
	li	s6, 93
	j	fail

	.balign	4
strap:
	li	t0, 1
	bne	s8, t0, wrong_s_handler
	csrr	t0, scause
	bne	t0, s1, wrong_s_cause
	csrr	t1, stval
	bne	t1, s2, wrong_s_tval
	addi	s3, s3, 1
	bnez	s5, 1f
	csrr	t0, sepc
	addi	t0, t0, 4
	j	2f
1:
	mv	t0, s5
	li	s5, 0
2:
	csrw	sepc, t0
	sret

wrong_s_handler:
	li	s6, 94
	j	lower_fail
wrong_s_cause:
	li	s6, 95
	j	lower_fail
wrong_s_tval:
	li	s6, 96
	j	lower_fail

lower_fail:
	li	s9, 2
	ecall
	j	.

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

	.data
	.balign	16
semiargs:
	.space	16

	.bss
	.balign	4096
root_pt:
	.space	4096
s_l1:
	.space	4096
s_l0:
	.space	4096
u_l1:
	.space	4096
u_l0:
	.space	4096
page_tables_end:

	.section .pma_normal0,"aw",@nobits
	.balign	4096
pma_normal0:
	.space	4096
	.section .pma_strong0,"aw",@nobits
	.balign	4096
pma_strong0:
	.space	4096
	.section .pma_noncache0,"aw",@nobits
	.balign	4096
pma_noncache0:
	.space	4096
	.section .pma_normal1,"aw",@nobits
	.balign	4096
pma_normal1:
	.space	4096
	.section .pma_strong1,"aw",@nobits
	.balign	4096
pma_strong1:
	.space	4096
	.section .pma_noncache1,"aw",@nobits
	.balign	4096
pma_noncache1:
	.space	4096
	.section .pma_normal2,"aw",@nobits
	.balign	4096
pma_normal2:
	.space	4096
	.section .pma_strong2,"aw",@nobits
	.balign	4096
pma_strong2:
	.space	4096
	.section .pma_default,"aw",@nobits
	.balign	4096
pma_default:
	.space	4096
