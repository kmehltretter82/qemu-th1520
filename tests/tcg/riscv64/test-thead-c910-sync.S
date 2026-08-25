/*
 * T-Head C910 XTheadSync privilege and MTTCG ordering test.
 *
 * Raw words keep the test independent of assembler support for XTheadSync.
 * Hart 0 checks all four encodings in M/S/U mode and THEADISAEE gating.  A
 * three-hart store-buffering litmus then puts th.sync* between each plain
 * store/load pair.  Atomics delimit iterations, but do not sit between either
 * ordered pair under test; every observed word has a separate cache line.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.option	norelax
	.option	norvc

#define CSR_TH_MXSTATUS		0x7c0
#define MXSTATUS_THEADISAEE	(1 << 22)

#define MSTATUS_MPP_MASK	(3 << 11)
#define MSTATUS_MPP_M		(3 << 11)
#define MSTATUS_MPP_S		(1 << 11)
#define MSTATUS_MPP_U		0

#define CAUSE_ILLEGAL		2
#define CAUSE_ECALL_U		8
#define CAUSE_ECALL_S		9

#define SYNC_ITERATIONS		4096

#define TH_SYNC			0x0180000b
#define TH_SYNC_S		0x0190000b
#define TH_SYNC_I		0x01a0000b
#define TH_SYNC_IS		0x01b0000b

	.macro	SET_MPP value
	csrr	t0, mstatus
	li	t1, MSTATUS_MPP_MASK
	not	t1, t1
	and	t0, t0, t1
	li	t1, \value
	or	t0, t0, t1
	csrw	mstatus, t0
	.endm

	.macro	SYNC_ALL
	.word	TH_SYNC
	.word	TH_SYNC_S
	.word	TH_SYNC_I
	.word	TH_SYNC_IS
	.endm

	.macro	EXPECT_ILLEGAL word
	li	s1, CAUSE_ILLEGAL
	li	s7, \word
	lla	s2, .Lexpected_\@
.Lexpected_\@:
	.word	\word
	bnez	s1, fail
	.endm

	.text
	.global	_start
_start:
	csrr	t0, mhartid
	beqz	t0, hart0
	li	t1, 1
	beq	t0, t1, participant
	li	t1, 2
	beq	t0, t1, coordinator
secondary_idle:
	wfi
	j	secondary_idle

hart0:
	lla	t0, mtrap
	csrw	mtvec, t0
	csrw	medeleg, zero
	li	s0, 0			/* Expected-illegal trap count. */
	li	s1, 0			/* Expected cause, or zero. */
	li	s5, 0			/* Lower-mode result. */

	/* Reset enables the extension and all variants are legal in M mode. */
	li	s3, 1
	csrr	t0, CSR_TH_MXSTATUS
	li	t1, MXSTATUS_THEADISAEE
	and	t2, t0, t1
	bne	t2, t1, fail
	SYNC_ALL

	/* The frozen extension permits every variant in S mode. */
	li	s3, 2
	lla	s4, after_s
	lla	t0, s_mode
	csrw	mepc, t0
	SET_MPP MSTATUS_MPP_S
	mret
after_s:

	/* It also permits every variant in U mode. */
	li	s3, 3
	lla	s4, after_u
	lla	t0, u_mode
	csrw	mepc, t0
	SET_MPP MSTATUS_MPP_U
	mret
after_u:

	/* THEADISAEE gates all four encodings with precise illegal traps. */
	li	s3, 4
	li	t0, MXSTATUS_THEADISAEE
	csrc	CSR_TH_MXSTATUS, t0
	csrr	t1, CSR_TH_MXSTATUS
	and	t1, t1, t0
	bnez	t1, fail
	EXPECT_ILLEGAL TH_SYNC
	EXPECT_ILLEGAL TH_SYNC_S
	EXPECT_ILLEGAL TH_SYNC_I
	EXPECT_ILLEGAL TH_SYNC_IS
	li	t0, 4
	bne	s0, t0, fail

	/* Re-enable before releasing harts 1 and 2 into the ordering test. */
	li	t0, MXSTATUS_THEADISAEE
	csrs	CSR_TH_MXSTATUS, t0
	SYNC_ALL

	li	s3, 5
	lla	t0, litmus_start
	li	t1, 1
	amoswap.w.rl zero, t1, (t0)
	li	s0, 0			/* Hart 0 is participant A. */
	jal	zero, participant_ready

participant_complete:
	lla	t0, litmus_complete
1:
	amoadd.w.aq t1, zero, (t0)
	beqz	t1, 1b
	lla	t0, ordering_error
	amoadd.w.aq t1, zero, (t0)
	bnez	t1, fail_ordering
	li	a0, 0
	j	exit

s_mode:
	SYNC_ALL
	li	s5, 1
	ecall
	j	.

u_mode:
	SYNC_ALL
	li	s5, 1
	ecall
	j	.

/* Harts 0 and 1 are the two store-buffering participants. */
participant:
	li	s0, 1			/* Hart 1 is participant B. */
participant_ready:
	lla	t0, litmus_start
2:
	amoadd.w.aq t1, zero, (t0)
	beqz	t1, 2b
	li	s1, 4 * SYNC_ITERATIONS
	li	s2, 0			/* Last epoch token. */
participant_next:
	lla	t0, epoch
3:
	amoadd.w.aq t2, zero, (t0)
	beq	t2, s2, 3b
	mv	s2, t2
	srli	t4, t2, 16
	addi	t4, t4, -1

	beqz	s0, participant_a_store
	lla	t0, litmus_y
	li	t3, 1
	sw	t3, 0(t0)
	j	participant_barrier
participant_a_store:
	lla	t0, litmus_x
	li	t3, 1
	sw	t3, 0(t0)

participant_barrier:
	beqz	t4, participant_sync
	li	t6, 1
	beq	t4, t6, participant_sync_s
	li	t6, 2
	beq	t4, t6, participant_sync_i
	.word	TH_SYNC_IS
	j	participant_barrier_done
participant_sync:
	.word	TH_SYNC
	j	participant_barrier_done
participant_sync_s:
	.word	TH_SYNC_S
	j	participant_barrier_done
participant_sync_i:
	.word	TH_SYNC_I
participant_barrier_done:
	beqz	s0, participant_a_load
	lla	t0, litmus_x
	lw	t3, 0(t0)
	lla	t0, result_b
	sw	t3, 0(t0)
	j	participant_done
participant_a_load:
	lla	t0, litmus_y
	lw	t3, 0(t0)
	lla	t0, result_a
	sw	t3, 0(t0)
participant_done:
	lla	t0, done_count
	li	t1, 1
	amoadd.w.rl zero, t1, (t0)
	addi	s1, s1, -1
	bnez	s1, participant_next
	bnez	s0, secondary_idle
	j	participant_complete

/* Hart 2 resets each trial and diagnoses the forbidden zero/zero outcome. */
coordinator:
	lla	t0, litmus_start
4:
	amoadd.w.aq t1, zero, (t0)
	beqz	t1, 4b
	li	s0, 0			/* Variant number. */
coordinator_variant:
	li	s1, 1			/* Iteration number. */
coordinator_iteration:
	lla	t0, litmus_x
	sw	zero, 0(t0)
	lla	t0, litmus_y
	sw	zero, 0(t0)
	lla	t0, result_a
	sw	zero, 0(t0)
	lla	t0, result_b
	sw	zero, 0(t0)
	lla	t0, done_count
	sw	zero, 0(t0)

	/* Publish only after every word from the prior trial is reset. */
	fence	rw, rw
	addi	t5, s0, 1
	slli	t5, t5, 16
	or	t5, t5, s1
	lla	t0, epoch
	amoswap.w.rl zero, t5, (t0)

	lla	t0, done_count
5:
	amoadd.w.aq t1, zero, (t0)
	li	t2, 2
	bne	t1, t2, 5b
	lla	t0, result_a
	lw	t1, 0(t0)
	lla	t0, result_b
	lw	t2, 0(t0)
	or	t1, t1, t2
	bnez	t1, coordinator_next
	lla	t0, ordering_error
	sw	t5, 0(t0)
coordinator_next:
	addi	s1, s1, 1
	li	t0, SYNC_ITERATIONS + 1
	bne	s1, t0, coordinator_iteration
	addi	s0, s0, 1
	li	t0, 4
	bne	s0, t0, coordinator_variant
	lla	t0, litmus_complete
	li	t1, 1
	amoswap.w.rl zero, t1, (t0)
	j	secondary_idle

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
	bne	s5, t0, lower_failure
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
lower_failure:
	/* s3 identifies the lower-mode phase. */
	j	fail
fail_ordering:
	li	s3, 6
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

	.data
	.balign	4096
litmus_start:
	.word	0
	.balign	4096
epoch:
	.word	0
	.balign	4096
litmus_x:
	.word	0
	.balign	4096
litmus_y:
	.word	0
	.balign	4096
result_a:
	.word	0
	.balign	4096
result_b:
	.word	0
	.balign	4096
done_count:
	.word	0
	.balign	4096
ordering_error:
	.word	0
	.balign	4096
litmus_complete:
	.word	0
	.balign	16
semiargs:
	.space	16
