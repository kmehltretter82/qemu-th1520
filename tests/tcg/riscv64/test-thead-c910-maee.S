/*
 * T-Head C910 MAEE page-attribute test.
 *
 * With MAEE enabled, openC910 sources PTE[63:59] as SO/C/B/SH/SEC.  Ordinary
 * scalar accesses remain unaligned on normal, non-cacheable and non-shareable
 * pages when MXSTATUS.MM is set, but a strong-order page requires natural
 * alignment after translation.  Instruction fetch from a strong-order page
 * raises an access fault.  With MAEE disabled, all five PTE bits are reserved.
 *
 * This test covers those architecturally visible rules in S and U mode.  M
 * mode owns MAEE and checks both reset state and the on/off transition.  It
 * deliberately does not claim cache, ordering, shareability or security-bus
 * effects which QEMU cannot yet represent.
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

#define PTE_V			0x001
#define PTE_S_RWX_AD		0x0cf
#define PTE_U_RWX_AD		0x0df
#define MAEE_NORMAL		(0xe << 59)
#define MAEE_NONCACHE		(0x6 << 59)
#define MAEE_STRONG		(0x12 << 59)
#define MAEE_NONSHARE		(0xc << 59)

#define SATP_SV39		(8 << 60)
#define USER_ALIAS_DELTA	0x80000000

#define S_NORMAL_VA		0x40000000
#define S_NONCACHE_VA		0x40001000
#define S_STRONG_VA		0x40002000
#define S_NONSHARE_VA		0x40003000
#define S_CROSS_NORMAL_VA	0x40004000
#define S_CROSS_STRONG_VA	0x40006000
#define U_NORMAL_VA		0x40008000
#define U_NONCACHE_VA		0x40009000
#define U_STRONG_VA		0x4000a000
#define U_NONSHARE_VA		0x4000b000
#define U_CROSS_NORMAL_VA	0x4000c000
#define U_CROSS_STRONG_VA	0x4000e000

#define PRV_U			0
#define PRV_M			3

#define CAUSE_INST_ACCESS	1
#define CAUSE_LOAD_MISALIGNED	4
#define CAUSE_STORE_MISALIGNED	6
#define CAUSE_ECALL_U		8
#define CAUSE_ECALL_S		9
#define CAUSE_LOAD_PAGE_FAULT	13
#define DELEGATED_EXCEPTIONS	0xb052

/*
 * Persistent registers:
 *   s0  = current attribute-test virtual page
 *   s1  = expected cause
 *   s2  = expected trap value
 *   s3  = trap counter
 *   s4  = trap-counter snapshot
 *   s5  = explicit recovery PC for an instruction access fault, or zero
 *   s6  = failure stage
 *   s7  = successful lower-mode return PC
 *   s8  = expected handler (0 = M, 1 = S)
 *   s9  = lower-mode control ecall (1 = success, 2 = failure)
 *   s11 = failure return PC
 */
	.macro	EXPECT handler, cause, addr
	li	s8, \handler
	li	s1, \cause
	mv	s2, \addr
	mv	s4, s3
	li	s5, 0
	.endm

	.macro	EXPECT_INSN handler, addr, recovery
	li	s8, \handler
	li	s1, CAUSE_INST_ACCESS
	mv	s2, \addr
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

	.macro	MAP_PAGE offset, page, flags
	lla	t1, \page
	srli	t1, t1, 2
	li	t2, \flags
	or	t1, t1, t2
	sd	t1, \offset(t0)
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

	/* MAEE and scalar misaligned access both reset enabled on C910. */
	li	s6, 1
	csrr	t0, CSR_TH_MXSTATUS
	li	t1, MXSTATUS_MAEE | MXSTATUS_MM
	and	t0, t0, t1
	bne	t0, t1, fail

	/* Clear every PTE so the payload can also be reused on hardware. */
	lla	t0, root_pt
	lla	t1, page_tables_end
1:
	sd	zero, 0(t0)
	addi	t0, t0, 8
	bltu	t0, t1, 1b

	/* root[1] -> fine-grained attribute aliases. */
	lla	t0, root_pt
	lla	t1, alias_l1
	srli	t1, t1, 2
	ori	t1, t1, PTE_V
	sd	t1, 8(t0)

	/* root[2] keeps M/S code and data identity-mapped with zero attrs. */
	li	t1, 0x80000000
	srli	t1, t1, 2
	ori	t1, t1, PTE_S_RWX_AD
	sd	t1, 16(t0)

	/* root[4] aliases that GiB for U-mode code with normal attributes. */
	li	t1, 0x80000000
	srli	t1, t1, 2
	li	t2, PTE_U_RWX_AD | MAEE_NORMAL
	or	t1, t1, t2
	sd	t1, 32(t0)

	/* The first 2 MiB under root[1] uses 4 KiB leaf mappings. */
	lla	t0, alias_l1
	lla	t1, alias_l0
	srli	t1, t1, 2
	ori	t1, t1, PTE_V
	sd	t1, 0(t0)

	lla	t0, alias_l0
	MAP_PAGE 0, probe_page, PTE_S_RWX_AD | MAEE_NORMAL
	MAP_PAGE 8, probe_page, PTE_S_RWX_AD | MAEE_NONCACHE
	MAP_PAGE 16, probe_page, PTE_S_RWX_AD | MAEE_STRONG
	MAP_PAGE 24, probe_page, PTE_S_RWX_AD | MAEE_NONSHARE
	MAP_PAGE 32, cross_page_a, PTE_S_RWX_AD | MAEE_NORMAL
	MAP_PAGE 40, cross_page_b, PTE_S_RWX_AD | MAEE_STRONG
	MAP_PAGE 48, cross_page_a, PTE_S_RWX_AD | MAEE_STRONG
	MAP_PAGE 56, cross_page_b, PTE_S_RWX_AD | MAEE_NORMAL
	MAP_PAGE 64, probe_page, PTE_U_RWX_AD | MAEE_NORMAL
	MAP_PAGE 72, probe_page, PTE_U_RWX_AD | MAEE_NONCACHE
	MAP_PAGE 80, probe_page, PTE_U_RWX_AD | MAEE_STRONG
	MAP_PAGE 88, probe_page, PTE_U_RWX_AD | MAEE_NONSHARE
	MAP_PAGE 96, cross_page_a, PTE_U_RWX_AD | MAEE_NORMAL
	MAP_PAGE 104, cross_page_b, PTE_U_RWX_AD | MAEE_STRONG
	MAP_PAGE 112, cross_page_a, PTE_U_RWX_AD | MAEE_STRONG
	MAP_PAGE 120, cross_page_b, PTE_U_RWX_AD | MAEE_NORMAL

	/* Install a tiny executable probe and deterministic unaligned data. */
	lla	t0, probe_page
	li	t1, 0x02a00513	/* addi a0, zero, 42 */
	sw	t1, 0(t0)
	li	t1, 0x00008067	/* ret */
	sw	t1, 4(t0)
	li	t1, 0x8877665544332211
	sd	t1, 16(t0)
	.word	0x0000100f	/* fence.i */

	lla	t0, cross_page_a
	li	t1, 4093
	add	t0, t0, t1
	li	t1, 0x11
	sb	t1, 0(t0)
	li	t1, 0x22
	sb	t1, 1(t0)
	li	t1, 0x33
	sb	t1, 2(t0)
	lla	t0, cross_page_b
	li	t1, 0x44
	sb	t1, 0(t0)

	lla	t0, root_pt
	srli	t0, t0, 12
	li	t1, SATP_SV39
	or	t0, t0, t1
	csrw	satp, t0
	sfence.vma

	/* First exercise MAEE-enabled mappings in S mode. */
	li	s6, 2
	lla	s7, after_s_maee_on
	lla	t0, s_mode_maee_on
	csrw	mepc, t0
	SET_MPP MSTATUS_MPP_S
	mret

after_s_maee_on:
	/* Only M can change MAEE; its write flushes prior translations. */
	li	t0, MXSTATUS_MAEE
	csrc	CSR_TH_MXSTATUS, t0
	sfence.vma
	li	s6, 20
	lla	s7, after_s_maee_off
	lla	t0, s_mode_maee_off
	csrw	mepc, t0
	SET_MPP MSTATUS_MPP_S
	mret

after_s_maee_off:
	li	t0, MXSTATUS_MAEE
	csrs	CSR_TH_MXSTATUS, t0
	sfence.vma

	/* U exceptions use the S handler; its completion ecall returns to M. */
	li	t0, DELEGATED_EXCEPTIONS
	csrw	medeleg, t0
	li	s6, 30
	lla	s7, after_u_maee_on
	lla	t0, u_mode_maee_on
	li	t1, USER_ALIAS_DELTA
	add	t0, t0, t1
	csrw	mepc, t0
	SET_MPP PRV_U
	mret

after_u_maee_on:
	li	s6, 40
	li	t0, 9
	bne	s3, t0, fail
	li	t0, MXSTATUS_MAEE | MXSTATUS_MM
	csrr	t1, CSR_TH_MXSTATUS
	and	t1, t1, t0
	bne	t1, t0, fail
	li	a0, 0
	j	exit

/* S-mode normal/non-cacheable/non-shareable pages permit scalar unalignment. */
s_mode_maee_on:
	li	s6, 3
	li	s0, S_NORMAL_VA
	lwu	a2, 17(s0)
	li	t0, 0x55443322
	bne	a2, t0, lower_fail
	li	s0, S_NONCACHE_VA
	lwu	a2, 17(s0)
	bne	a2, t0, lower_fail
	li	s0, S_NONSHARE_VA
	lwu	a2, 17(s0)
	bne	a2, t0, lower_fail

	/* Strong-order data works aligned. */
	/* Unaligned data traps after the page-table walk. */
	li	s6, 4
	li	s0, S_STRONG_VA
	ld	a2, 16(s0)
	li	t0, 0x8877665544332211
	bne	a2, t0, lower_fail
	addi	t4, s0, 17
	EXPECT 0, CAUSE_LOAD_MISALIGNED, t4
	lwu	a2, 17(s0)
	CHECK_TRAP
	li	t6, 0xdeadbeef
	EXPECT 0, CAUSE_STORE_MISALIGNED, t4
	sw	t6, 17(s0)
	CHECK_TRAP

	/* A weak first page permits an unaligned access across an SO page. */
	li	s6, 5
	li	s0, S_CROSS_NORMAL_VA
	li	t1, 4093
	add	t4, s0, t1
	lwu	a2, 0(t4)
	li	t0, 0x44332211
	bne	a2, t0, lower_fail

	/* A strong first page requires alignment with a normal second page. */
	li	s6, 6
	li	s0, S_CROSS_STRONG_VA
	add	t4, s0, t1
	EXPECT 0, CAUSE_LOAD_MISALIGNED, t4
	lwu	a2, 0(t4)
	CHECK_TRAP

	/* Non-cacheable fetch is valid; strong-order fetch is not. */
	li	s6, 7
	li	t4, S_NORMAL_VA
	jalr	ra, t4, 0
	li	t0, 42
	bne	a0, t0, lower_fail
	li	t4, S_NONCACHE_VA
	jalr	ra, t4, 0
	bne	a0, t0, lower_fail
	li	t4, S_NONSHARE_VA
	jalr	ra, t4, 0
	bne	a0, t0, lower_fail

	/* The earlier aligned load left a data-side SO translation cached. */
	li	s6, 8
	li	t4, S_STRONG_VA
	EXPECT_INSN 0, t4, s_after_strong_fetch
	jalr	ra, t4, 0
	j	lower_fail
s_after_strong_fetch:
	CHECK_TRAP
	li	s9, 1
	ecall
	j	lower_fail

/* With MAEE clear, PTE[63:59] is reserved and produces a page fault. */
s_mode_maee_off:
	li	s6, 21
	li	s0, S_NORMAL_VA
	mv	t4, s0
	EXPECT 0, CAUSE_LOAD_PAGE_FAULT, t4
	ld	a2, 0(s0)
	CHECK_TRAP
	li	s9, 1
	ecall
	j	lower_fail

/* Repeat the observable attribute behavior with U-to-S delegated traps. */
u_mode_maee_on:
	li	s6, 31
	li	s0, U_NORMAL_VA
	lwu	a2, 17(s0)
	li	t0, 0x55443322
	bne	a2, t0, lower_fail
	li	s0, U_NONCACHE_VA
	lwu	a2, 17(s0)
	bne	a2, t0, lower_fail
	li	s0, U_NONSHARE_VA
	lwu	a2, 17(s0)
	bne	a2, t0, lower_fail

	li	s6, 32
	li	s0, U_STRONG_VA
	ld	a2, 16(s0)
	li	t0, 0x8877665544332211
	bne	a2, t0, lower_fail
	addi	t4, s0, 17
	EXPECT 1, CAUSE_LOAD_MISALIGNED, t4
	lwu	a2, 17(s0)
	CHECK_TRAP
	li	t6, 0xcafebabe
	EXPECT 1, CAUSE_STORE_MISALIGNED, t4
	sw	t6, 17(s0)
	CHECK_TRAP

	li	s6, 33
	li	s0, U_CROSS_NORMAL_VA
	li	t1, 4093
	add	t4, s0, t1
	lwu	a2, 0(t4)
	li	t0, 0x44332211
	bne	a2, t0, lower_fail
	li	s0, U_CROSS_STRONG_VA
	add	t4, s0, t1
	EXPECT 1, CAUSE_LOAD_MISALIGNED, t4
	lwu	a2, 0(t4)
	CHECK_TRAP

	li	s6, 34
	li	t4, U_NORMAL_VA
	jalr	ra, t4, 0
	li	t0, 42
	bne	a0, t0, lower_fail
	li	t4, U_NONCACHE_VA
	jalr	ra, t4, 0
	bne	a0, t0, lower_fail
	li	t4, U_NONSHARE_VA
	jalr	ra, t4, 0
	bne	a0, t0, lower_fail

	li	s6, 35
	li	t4, U_STRONG_VA
	EXPECT_INSN 1, t4, u_after_strong_fetch
	jalr	ra, t4, 0
	j	lower_fail
u_after_strong_fetch:
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
alias_l1:
	.space	4096
alias_l0:
	.space	4096
page_tables_end:

	.balign	4096
probe_page:
	.space	4096
cross_page_a:
	.space	4096
cross_page_b:
	.space	4096
