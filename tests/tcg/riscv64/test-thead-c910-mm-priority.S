/*
 * T-Head C910 MXSTATUS.MM exception-priority test.
 *
 * openC910 checks ordinary scalar alignment before translation when MM is
 * clear.  When MM is set, a misaligned scalar access can reach translation,
 * so a fault in its second page takes precedence.  Atomic and vector memory
 * operations remain aligned-only and therefore trap before translation.
 *
 * This test builds a guarded Sv39 mapping and checks those rules in S mode
 * and U mode.  U-mode exceptions are delegated to S mode.  It also verifies
 * that a cross-page faulting store has no visible first-page side effect in
 * the QEMU model; physical-silicon comparison remains required.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.option	norelax
	.option	norvc

#define CSR_TH_MXSTATUS		0x7c0
#define CSR_TH_SXSTATUS		0x5c0
#define MXSTATUS_MM		(1 << 15)

#define MSTATUS_MPP_MASK	(3 << 11)
#define MSTATUS_MPP_S		(1 << 11)
#define MSTATUS_MPP_M		(3 << 11)
#define MSTATUS_FS_INITIAL	(1 << 13)
#define MSTATUS_SUM		(1 << 18)
#define MSTATUS_VS_THEAD	(1 << 23)

#define PTE_V			0x001
#define PTE_R			0x002
#define PTE_W			0x004
#define PTE_X			0x008
#define PTE_U			0x010
#define PTE_A			0x040
#define PTE_D			0x080
#define PTE_S_RWX_AD		0x0cf
#define PTE_U_RWX_AD		0x0df
#define PTE_U_RW_AD		0x0d7

#define SATP_SV39		(8 << 60)
#define GUARD_VA		0x40000000
#define USER_ALIAS_DELTA	0x80000000

#define PRV_U			0
#define PRV_S			1
#define PRV_M			3

#define CAUSE_LOAD_MISALIGNED	4
#define CAUSE_STORE_MISALIGNED	6
#define CAUSE_ECALL_U		8
#define CAUSE_ECALL_S		9
#define CAUSE_LOAD_PAGE_FAULT	13
#define CAUSE_STORE_PAGE_FAULT	15
#define DELEGATED_EXCEPTIONS	0xa050

/*
 * Persistent registers:
 *   s0  = guarded virtual page
 *   s1  = expected cause
 *   s2  = expected trap value
 *   s3  = trap counter
 *   s4  = trap-counter snapshot
 *   s6  = failure stage
 *   s7  = successful lower-mode return PC
 *   s8  = expected handler (0 = M, 1 = S)
 *   s9  = lower-mode control ecall (1 = success, 2 = failure)
 *   s11 = failure return PC
 */
	.macro	EXPECT handler, cause, base, off
	li	s8, \handler
	li	s1, \cause
	li	s2, \off
	add	s2, s2, \base
	mv	s4, s3
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

	.text
	.global	_start
_start:
	lla	t0, mtrap
	csrw	mtvec, t0
	lla	t0, strap
	csrw	stvec, t0
	csrw	medeleg, zero
	li	s3, 0
	li	s9, 0
	lla	s11, fail

	/* Clear page tables explicitly so hardware can reuse the payload. */
	lla	t0, root_pt
	lla	t1, page_tables_end
1:
	sd	zero, 0(t0)
	addi	t0, t0, 8
	bltu	t0, t1, 1b

	/* root[1] -> guard L1; root[2] is the supervisor identity map. */
	lla	t0, root_pt
	lla	t1, guard_l1
	srli	t1, t1, 2
	ori	t1, t1, PTE_V
	sd	t1, 8(t0)
	li	t1, 0x80000000
	srli	t1, t1, 2
	ori	t1, t1, PTE_S_RWX_AD
	sd	t1, 16(t0)

	/* root[4] aliases the same physical GiB as executable user memory. */
	li	t1, 0x80000000
	srli	t1, t1, 2
	ori	t1, t1, PTE_U_RWX_AD
	sd	t1, 32(t0)

	/* Map only the first 4 KiB of GUARD_VA as user-readable/writable. */
	lla	t0, guard_l1
	lla	t1, guard_l0
	srli	t1, t1, 2
	ori	t1, t1, PTE_V
	sd	t1, 0(t0)
	lla	t0, guard_l0
	lla	t1, guard_page
	srli	t1, t1, 2
	ori	t1, t1, PTE_U_RW_AD
	sd	t1, 0(t0)

	/* Known data at both ends detects successful and partial accesses. */
	lla	t0, guard_page
	li	t1, 0x8877665544332211
	sd	t1, 0(t0)
	li	t2, 4096 - 8
	add	t0, t0, t2
	li	t1, 0x1122334455667788
	sd	t1, 0(t0)

	li	t0, MSTATUS_FS_INITIAL | MSTATUS_SUM | MSTATUS_VS_THEAD
	csrs	mstatus, t0
	li	t0, MXSTATUS_MM
	csrs	CSR_TH_MXSTATUS, t0

	lla	t0, root_pt
	srli	t0, t0, 12
	li	t1, SATP_SV39
	or	t0, t0, t1
	csrw	satp, t0
	sfence.vma

	/* S-mode faults are handled in M mode. */
	li	s6, 1
	lla	s7, after_s_mode
	lla	t0, s_mode_entry
	csrw	mepc, t0
	SET_MPP MSTATUS_MPP_S
	mret

after_s_mode:
	/* Delegate U-mode alignment and page faults to the S-mode handler. */
	li	t0, DELEGATED_EXCEPTIONS
	csrw	medeleg, t0
	li	t0, MXSTATUS_MM
	csrs	CSR_TH_MXSTATUS, t0

	li	s6, 10
	lla	s7, after_u_mm_on
	lla	t0, u_mode_mm_on
	li	t1, USER_ALIAS_DELTA
	add	t0, t0, t1
	csrw	mepc, t0
	SET_MPP PRV_U
	mret

after_u_mm_on:
	li	t0, MXSTATUS_MM
	csrc	CSR_TH_MXSTATUS, t0
	li	s6, 14
	lla	s7, after_u_mm_off
	lla	t0, u_mode_mm_off
	li	t1, USER_ALIAS_DELTA
	add	t0, t0, t1
	csrw	mepc, t0
	SET_MPP PRV_U
	mret

after_u_mm_off:
	li	s6, 18
	li	t0, 23
	bne	s3, t0, fail
	li	t0, MXSTATUS_MM
	csrr	t1, CSR_TH_MXSTATUS
	and	t1, t1, t0
	bnez	t1, fail
	li	a0, 0
	j	exit

/* MM set: ordinary S-mode scalar accesses can be misaligned. */
s_mode_entry:
	li	s0, GUARD_VA
	li	s6, 2
	EXPECT	0, CAUSE_LOAD_MISALIGNED, s0, 1
	lw	a2, 1(s0)
	CHECK_NO_TRAP
	li	t0, 0x55443322
	bne	a2, t0, lower_fail
	li	t6, 0xaabbccdd
	EXPECT	0, CAUSE_STORE_MISALIGNED, s0, 1
	sw	t6, 1(s0)
	CHECK_NO_TRAP
	lwu	t0, 1(s0)
	bne	t0, t6, lower_fail

	/* An aligned guard access establishes page-fault routing. */
	li	s6, 3
	li	t4, 4096
	add	t4, s0, t4
	EXPECT	0, CAUSE_LOAD_PAGE_FAULT, s0, 4096
	lw	a2, 0(t4)
	CHECK_TRAP

	/* With MM set, a second-page fault wins over scalar misalignment. */
	li	s6, 4
	li	t4, 4093
	add	t4, s0, t4
	EXPECT	0, CAUSE_LOAD_PAGE_FAULT, s0, 4096
	lw	a2, 0(t4)
	CHECK_TRAP
	li	t6, 0xdeadbeef
	EXPECT	0, CAUSE_STORE_PAGE_FAULT, s0, 4096
	sw	t6, 0(t4)
	CHECK_TRAP
	/* QEMU resolves both translations before committing the store. */
	lbu	t0, 0(t4)
	li	t1, 0x33
	bne	t0, t1, lower_fail
	lbu	t0, 1(t4)
	li	t1, 0x22
	bne	t0, t1, lower_fail
	lbu	t0, 2(t4)
	li	t1, 0x11
	bne	t0, t1, lower_fail

	/* Atomics and XTheadVector still trap before page translation. */
	li	s6, 5
	li	t4, 4093
	add	t4, s0, t4
	EXPECT	0, CAUSE_LOAD_MISALIGNED, s0, 4093
	lr.w	t5, (t4)
	CHECK_TRAP
	EXPECT	0, CAUSE_STORE_MISALIGNED, s0, 4093
	amoswap.w	t5, t6, (t4)
	CHECK_TRAP
	li	t1, 1
	.word	0x008372d7	/* th.vsetvli t0, t1, e32, m1 */
	mv	t2, t4
	EXPECT	0, CAUSE_LOAD_MISALIGNED, s0, 4093
	.word	0x1203e087	/* th.vlw.v v1, (t2) */
	CHECK_TRAP
	EXPECT	0, CAUSE_STORE_MISALIGNED, s0, 4093
	.word	0x0203e0a7	/* th.vsw.v v1, (t2) */
	CHECK_TRAP

	/* S mode can clear MM through SXSTATUS; misalignment now wins. */
	li	s6, 6
	li	t0, MXSTATUS_MM
	csrc	CSR_TH_SXSTATUS, t0
	EXPECT	0, CAUSE_LOAD_MISALIGNED, s0, 1
	lw	a2, 1(s0)
	CHECK_TRAP
	EXPECT	0, CAUSE_STORE_MISALIGNED, s0, 1
	sw	t6, 1(s0)
	CHECK_TRAP

	li	s6, 7
	li	t4, 4093
	add	t4, s0, t4
	EXPECT	0, CAUSE_LOAD_MISALIGNED, s0, 4093
	lw	a2, 0(t4)
	CHECK_TRAP
	EXPECT	0, CAUSE_STORE_MISALIGNED, s0, 4093
	sw	t6, 0(t4)
	CHECK_TRAP

	li	s6, 8
	li	t0, MXSTATUS_MM
	csrs	CSR_TH_SXSTATUS, t0
	li	s9, 1
	ecall
	j	lower_fail

/* MM set: repeat the guarded scalar/atomic/vector split in U mode. */
u_mode_mm_on:
	li	s0, GUARD_VA
	li	s6, 10
	EXPECT	1, CAUSE_LOAD_MISALIGNED, s0, 1
	lw	a2, 1(s0)
	CHECK_NO_TRAP

	li	s6, 11
	li	t4, 4096
	add	t4, s0, t4
	EXPECT	1, CAUSE_LOAD_PAGE_FAULT, s0, 4096
	lw	a2, 0(t4)
	CHECK_TRAP
	li	t4, 4093
	add	t4, s0, t4
	EXPECT	1, CAUSE_LOAD_PAGE_FAULT, s0, 4096
	lw	a2, 0(t4)
	CHECK_TRAP
	li	t6, 0x12345678
	EXPECT	1, CAUSE_STORE_PAGE_FAULT, s0, 4096
	sw	t6, 0(t4)
	CHECK_TRAP

	li	s6, 12
	EXPECT	1, CAUSE_LOAD_MISALIGNED, s0, 4093
	lr.w	t5, (t4)
	CHECK_TRAP
	EXPECT	1, CAUSE_STORE_MISALIGNED, s0, 4093
	amoswap.w	t5, t6, (t4)
	CHECK_TRAP
	li	t1, 1
	.word	0x008372d7	/* th.vsetvli t0, t1, e32, m1 */
	mv	t2, t4
	EXPECT	1, CAUSE_LOAD_MISALIGNED, s0, 4093
	.word	0x1203e087	/* th.vlw.v v1, (t2) */
	CHECK_TRAP
	EXPECT	1, CAUSE_STORE_MISALIGNED, s0, 4093
	.word	0x0203e0a7	/* th.vsw.v v1, (t2) */
	CHECK_TRAP

	li	s6, 13
	li	s9, 1
	ecall
	j	lower_fail

/* MM clear: U-mode scalar misalignment wins before page translation. */
u_mode_mm_off:
	li	s0, GUARD_VA
	li	s6, 14
	EXPECT	1, CAUSE_LOAD_MISALIGNED, s0, 1
	lw	a2, 1(s0)
	CHECK_TRAP
	li	t6, 0xcafebabe
	EXPECT	1, CAUSE_STORE_MISALIGNED, s0, 1
	sw	t6, 1(s0)
	CHECK_TRAP

	li	s6, 15
	li	t4, 4093
	add	t4, s0, t4
	EXPECT	1, CAUSE_LOAD_MISALIGNED, s0, 4093
	lw	a2, 0(t4)
	CHECK_TRAP
	EXPECT	1, CAUSE_STORE_MISALIGNED, s0, 4093
	sw	t6, 0(t4)
	CHECK_TRAP

	/* An aligned access to the missing mapping still page-faults. */
	li	s6, 16
	li	t4, 4096
	add	t4, s0, t4
	EXPECT	1, CAUSE_LOAD_PAGE_FAULT, s0, 4096
	lw	a2, 0(t4)
	CHECK_TRAP

	li	s6, 17
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
	csrr	t0, mepc
	addi	t0, t0, 4
	csrw	mepc, t0
	mret

lower_control_trap:
	csrr	t0, mcause
	li	t1, CAUSE_ECALL_U
	beq	t0, t1, 1f
	li	t1, CAUSE_ECALL_S
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
	csrr	t0, sepc
	addi	t0, t0, 4
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
guard_l1:
	.space	4096
guard_l0:
	.space	4096
page_tables_end:

	.balign	4096
guard_page:
	.space	4096
