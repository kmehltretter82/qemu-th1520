# BeagleV Ahead local implementation audit

This document is the local counterpart to
[the upstream QEMU bug handoff](beaglev-ahead-upstream-bugs.md).  It covers
defects and limitations introduced by the BeagleV Ahead/TH1520 board and C910
work in this branch.  These findings are not claims about pre-existing
upstream QEMU, and must not be filed as upstream bugs without an independent
baseline reproducer.

* Audit checkpoint: 2026-08-28
* Branch: `beaglev-ahead`
* Latest-finding pre-fix workspace HEAD: `e017a59bba`
* Pinned upstream comparison: `bde2492aace2b5acb755a5b057013e915163a77f`

## Current disposition

The sixteen local findings recorded during implementation (`UQ-L001` through
`UQ-L016`) are fixed and have focused regressions.  The GMAC audit found that
the reusable DWC GMAC transmit path advertised checksum offload while its
inherited shortcut handled only IPv4 TCP/UDP, treated CIC2 and CIC3 alike, and
did not model the feature/TSF gates or descriptor-format error status.  At the
pre-fix checkpoint, the new IPv6, CIC2, TSF and status contract would fail;
for example, an IPv6 CIC3 frame retained its guest checksum.  Checkpoint
``f441cf709f`` replaces that shortcut with the bounded engine described under
``GMAC-001``.

Independent source and test reviews corrected the local policy before the
checkpoint: trailing bytes are stuff rather than an error, only computed-zero
UDP is mapped to ``0xffff``, TSF gates the complete engine, normal ES excludes
IHE/IPE, and short-payload versus UDP-length-mismatch tests are independent.
The 18-case matrix is clean in normal, dependency-minimal and sanitizer builds.
Uncertain silicon behavior remains under ``GMAC-001`` rather than being
labeled a bug.

The current local-QEMU checkpoint also repairs inherited scalar legality
behavior without opening or updating an upstream report: shift-zero
``th.addsl`` no longer bypasses extension/THEADISAEE checks, two XTheadCmo
privilege entries now follow the frozen specification and C910 manual, and
C910 U-mode CMO translation follows the writable UCME bit.  These are not
assigned new ``UQ-L`` identifiers because the affected translator predates the
branch-only board implementation.  Hardware conflict and comparison work is
kept under ``CPU-015`` in the validation ledger.

The preceding board-local finding was a scheduler-observability bug in the
TH1520 AP clock controller.  A tight Linux-style PLL poll could advance
virtual time beyond the modeled lock deadline before the I/O thread dispatched
the timer callback, leaving ``PLL_STS`` stale until after Linux's timeout.
QEMU now materializes an expired deadline when that status register is read as
well as in the existing timer callback.  The same 21.25 microsecond modeled
delay, reset behavior and migration state are retained.  A raw RV64 qtest
reproduces the old failure under single-threaded TCG and passes before and
after system reset with the fix.

The preceding scalar-index checkpoint fixes two RV64 wrong-result defects
in the vendor-derived XTheadVector helpers.  ``th.vslidedown.vx`` could wrap an
XLEN-wide offset back into the source group before checking VLMAX, while
``th.vrgather.vx`` truncated its helper-path scalar index to 32 bits.  Checkpoint
``9eef55f462`` uses overflow-safe slide bounds and retains a ``target_ulong``
gather index.  An independent scalar-RV64 oracle covers the affected helpers and
the existing optimized gather path; restoring the wrap, truncation and a
slide-boundary off-by-one makes it fail at exits 3, 9 and 13 respectively.
Current upstream QEMU has no
XTheadVector implementation, so this remains a public-series/vendor-fork review
finding rather than a report against released upstream QEMU.

Checkpoint ``78ad4d6e56`` fixes two nearby vector-permutation/slide defects.
``th.vrgather.vv`` truncated e64 indices to 32 bits, and the three slide-down
forms used the generic LMUL=1 mask-overlap allowance despite frozen RVV 0.7.1's
instruction-specific masked-``vd=v0`` prohibition.  The augmented sixth guest
uses an independent e64,m1 oracle, exact illegal traps and legal controls;
isolated mutations fail at exits 14 and 15.

Checkpoint ``1369cec4d9`` implements the DWMAC DMA receive-interrupt watchdog
needed by the advertised 3.70/Linux path.  It adds exact low-byte masking,
256-cycle timing, RI/NIS/PLIC delivery, cancellation/reset and version-2 timer
migration while preserving the reusable NPCM version-1/default-zero-clock
contract.  This is a local missing-device-behavior finding, not a report
against released upstream QEMU.

Remaining items are fidelity gaps or hardware questions, not confirmed bugs;
they stay open until an authoritative specification or an owner-board capture
establishes the expected behavior.

Detailed historical reproducer and fix records for the earlier findings remain
in the `UQ-L*` sections of
[the upstream handoff](beaglev-ahead-upstream-bugs.md).  Per owner direction,
the branch-local GMAC and PLL findings are recorded only here.  This file is
the short review index and current test checkpoint.

## Fixed local findings

| ID | Area | Original defect | Current state and regression |
| --- | --- | --- | --- |
| UQ-L001 | Board boot | The board accepted `-dtb` but ignored the external DTB. | Fixed by loading the supplied DTB; normal and sanitizer external-DTB qtests pass. |
| UQ-L002 | C910 ISA | The local CPU advertised `Zfh` without the required `Zfhmin` dependency, so half-precision operations trapped. | Fixed in the C910 extension table; C910 half-precision TCG coverage passes. |
| UQ-L003 | XTheadVector memory | Misaligned legacy vector loads/stores were accepted. | Fixed with the C910 alignment metadata; guarded-page and alignment-priority payloads pass. |
| UQ-L004 | Decoder ordering | Standard RVV translation could claim an encoding that belongs to XTheadVector. | Fixed by requiring `Zve32x` before ratified-RVV translation; the generic RVV and C910 payloads pass. |
| UQ-L005 | C910 MAEE | PTE attribute bits faulted when `MXSTATUS.MAEE` was clear, contrary to the openC910-derived rule. | Fixed by keeping the bits legal in both states and selecting attributes only when MAEE is enabled; the 38-case PMA payload passes. |
| UQ-L006 | XTheadVector CSR | Illegal `th.vsetvl` used the source register as scratch and changed it. | Fixed with a temporary operand; register and immediate WARL tests pass. |
| UQ-L007 | XTheadVector FP/status | FP and reduction paths missed FS/VS legality, FS-Dirty updates, exception flags, or unsupported widths. | Fixed before helper dispatch and around FP helpers; the FP/status payload passes. |
| UQ-L008 | XTheadVector reductions | `vl=0`, nonzero `vstart`, LMUL=8 widening, and widening-dispatch bounds were mishandled. | Fixed in helpers and dispatch validation; the reduction and standard-RVV bounds payloads pass. |
| UQ-L009 | XTheadVector mask query | `th.vmfirst.m` executed when `vstart` was nonzero instead of raising an illegal-instruction exception. | Fixed by requiring zero `vstart` in the translator; the state payload proves the trap, destination and `vstart` preservation, first-set index and no-hit result. |
| UQ-L010 | XTheadVector overlap | Masked comparison and mask-prefix destinations could overlap implicit source `v0` at LMUL greater than one. | Fixed in the four comparison checkers and mask-prefix macro; the overlap payload proves eight illegal forms, preservation of `v0`/`vstart` after the first trap, and legal LMUL=1, unmasked and non-`v0` controls. |
| UQ-L011 | DWC GMAC TX checksum | The local reusable/TH1520 integration collapsed CIC2 and CIC3 into the same IPv4 TCP/UDP recalculation and did not enforce the advertised TXCOESEL/store-and-forward contract, leaving IPv6 offload and descriptor error status incomplete. | Fixed with bounded CIC0-3 IPv4/IPv6 TCP/UDP/ICMP insertion, first/terminal split-frame handling and format-aware IHE/IPE/ES writeback; an 18-case BeagleV matrix and one NPCM normal-descriptor CIC3 compatibility case cover the correction. |
| UQ-L012 | TH1520 PLL polling | A guest could observe virtual time beyond a due PLL-lock deadline while ``PLL_STS`` remained stale until the I/O thread dispatched its timer callback. | Fixed by materializing an expired deadline on ``PLL_STS`` reads through the existing lock helper; the raw RV64 single-threaded-TCG qtest fails with the preserved pre-fix binary and passes twice, across reset, with the fix. |
| UQ-L013 | XTheadVector scalar permutation | `th.vslidedown.vx` could wrap a full-XLEN offset into a valid source lane, and the `th.vrgather.vx` helper truncated its scalar index to 32 bits. | Fixed with overflow-safe slide bounds and a `target_ulong` gather index; an independent scalar oracle covers helper/optimized, mask/prestart/tail, in-place and boundary paths, and mutations fail at exits 3, 9 and 13. |
| UQ-L014 | XTheadVector vector permutation/slide legality | `th.vrgather.vv` truncated e64 vector indices to 32 bits, and the slide-down translators missed the frozen v0.7.1 instruction-specific masked-`vd=v0` prohibition at LMUL=1. | Fixed in `78ad4d6e56` with a `uint64_t` gather index and dedicated slide checker; an e64,m1 oracle, three exact illegal traps, state preservation and legal controls pass, while mutations fail at exits 14 and 15. |
| UQ-L016 | DWC GMAC partial transmit frame | A multi-descriptor frame whose terminal segment was not yet owned by the DMA was accumulated in locals of one transmit call, so the engine's suspend discarded every segment already read; the resumed frame carried only the segments submitted after the suspend, and no migration stream carried the partial frame at all. | Fixed by giving the transmit engine ownership of the partial frame: it lives in device state, resets with the device, and migrates through a per-identity `tx-frame` subsection emitted only while a frame is suspended. A two-segment qtest fails before the fix with a 64-byte frame instead of 128, and a matching migration qtest fails when the subsection is removed. |
| UQ-L015 | DWC GMAC receive-interrupt watchdog | The local DWMAC 3.70/Ahead path advertised and Linux enabled RIWT mitigation, but the register did not schedule RI, so DIC-suppressed receive completions could remain unreported. | Fixed in `1369cec4d9` with low-byte RIWT timing, RI/NIS/PLIC delivery, cancellation/reset and VMState v2; focused watchdog/migration tests pass and the GMAC group passes 12/12. |

## Audit evidence

All commands below were run from the QEMU source directory.  Qtest commands
use the locally built QEMU through `QTEST_QEMU_BINARY`; sanitizer runs set
`ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer cannot operate reliably
under the qtest parent/ptrace setup.  The ASan `makecontext` warning is emitted
by the runtime and was not accompanied by an ASan or UBSan finding.

### Current board-gate infrastructure checkpoint

``beaglev-ahead-test`` was never added to ``slow_qtests`` in
``tests/qtest/meson.build``, so ``meson test`` applied the default 60-second
budget and terminated the complete board suite after roughly half its cases
(``Too few tests run (expected 149, got 78)``).  Every recent complete-gate
number was therefore obtained by running the test binary directly.  The suite
now declares 600 seconds, which is comfortable against the observed 74-second
normal-build runtime and the slower sanitizer build.

Restoring the meson path immediately failed
``/riscv64/beaglev-ahead/dw-gpio/registers``: GPIO2 returned ``0xddffffff``
from ``EXT_PORTA`` where the test expected ``0xffffffff``.  The two clear bits
are 25 and 29, the AP6203BM ``WL_HW_OOB`` and ``HOST_WAKE_BT`` inputs added at
``e017a59bba``.  ``dw_apb_gpio_update_pins()`` intentionally gives an external
driver priority over the internal one and logs the conflict, so a bank with
externally driven pins cannot read back everything the guest drives.  The
device model, board wiring and all-low reset convention are correct and
unchanged; the test now computes the externally driven mask and level per
controller.  That also makes the GMAC0 PHY interrupt on GPIO3_22 explicit,
where the previous ad-hoc expression happened to pass only because the
active-low line idles high.

This is recorded as a test defect rather than a ``UQ-L`` entry: no
guest-visible device behavior was wrong, and the promotion rule below requires
a stated expected result that the implementation violates.  The lesson is
procedural -- ``e017a59bba`` was committed with focused tests only, without a
complete-suite rerun that the timeout would have prevented anyway.

The complete board gate now passes 153/153 normal, 152/152
dependency-minimal and 152/152 ASan/UBSan at ``e017a59bba`` plus these two
corrections.

The remaining cited gates were re-run at the same commit and were all still
accurate, so only the board-gate number changed: ``riscv-csr-test`` passes
14/14, and the complete normal RISC-V TCG gate passed 37/37 with no failures.
``make check-tcg`` cannot be used for the dependency-minimal or sanitizer
builds, because they provide only the ``beaglev-ahead`` machine and the
``virt``-based payloads abort with ``unsupported machine type: "virt"``; the
Ahead-machine targets must be named explicitly.  With the two permutation
payloads added below those totals are now 39/39 normal and 16/16 for the
enumerated Ahead-specific subset, and all eight XTheadVector payloads pass
under ASan/UBSan when run directly on the ``beaglev-ahead`` machine with
``detect_leaks=0``.

### Current XTheadVector slide-up checkpoint

Of the ten XTheadVector permutation instructions, five had no dynamic
coverage at all: ``th.vslideup.vx``, ``th.vslideup.vi``, ``th.vslide1up.vx``,
``th.vrgather.vi`` and ``th.vcompress.vm``.  The slide-down and gather forms
next to them produced four wrong-result or illegal-encoding defects
(``UQ-L013`` and ``UQ-L014``), so the untested neighbours were the obvious
next gate.

A seventh payload, ``tests/tcg/riscv64/test-xtheadvector-slideup.S``, covers
the three slide-up forms.  Its independent scalar oracle checks offset 2, a
zero offset, an offset at vl, a full-XLEN ``0xffffffffffffffff`` offset, a
masked body, a nonzero ``vstart`` above the offset, the immediate form, and
``th.vslide1up.vx`` with and without a skipped element zero.  Six illegal
encodings must trap: masked ``vd=v0`` for all three forms, and the in-place
``vd=vs2`` encoding that frozen RVV 0.7.1 forbids for slide-up because lower
lanes are written before the higher source lanes are read.  Unmasked
``vd=v0``, a non-overlapping masked destination and the in-place *slide-down*
encoding are exercised as legal controls.

No defect was found: the implementation already satisfies every case.  The
payload is mutation-sensitive rather than vacuous.  Replacing the helper's
``i_min = MAX(env->vstart, offset)`` with ``i_min = env->vstart`` fails at exit
1, and removing ``(a->rd != a->rs2)`` from ``slideup_check_th`` fails at exit
23.  Both mutations were applied to a scratch copy and reverted.

### Current GMAC partial-transmit-frame checkpoint (UQ-L016)

This is the first ``MIG-001`` in-flight DMA item to close, and it found a
defect before it found a migration gap.  The reusable DWC GMAC transmit engine
kept a multi-descriptor frame in locals of one ``gmac_try_send_next_packet()``
call.  Whenever the next descriptor was still owned by software the engine
suspended and returned, discarding every segment it had already read from
guest memory; the guest then supplied the terminal segment, the resumed call
started from zero, and the frame went out carrying only that last segment.

A two-descriptor reproducer submits a 64-byte first segment with ``OWN`` set
and a 64-byte terminal segment with ``OWN`` clear, waits for ``TU`` and the
descriptor pointer to advance, hands over the terminal segment and polls.
Before the fix the wire carried 64 bytes instead of 128.  This is a real
guest-visible truncation reachable by any driver that fills a ring
incrementally.

The fix gives the transmit engine ownership of the partial frame: the
accumulated bytes, their length and the first-descriptor checksum control
live in ``DWGMACState``, a first segment starts a new frame and drops any
stale one, every error exit discards it, and device reset clears it.  The
host-side allocation capacity is not migrated; it is rebuilt from the length.

Migration carries the frame through a subsection that is only emitted while a
frame is suspended, so an idle GMAC and every existing stream are unchanged.
``savevm`` requires a subsection name to be prefixed by its parent's, and the
reusable core is registered under both ``dw-gmac`` and ``npcm-gmac``, so each
identity has its own description over one shared field table.  The
``-dump-vmstate`` output lists both ``dw-gmac/tx-frame`` and
``npcm-gmac/tx-frame``.  The shared-model check was not academic: a first
version with one ``dw-gmac``-prefixed subsection passed the complete Ahead
gate and aborted the NPCM suite on that exact assertion.

The matching migration qtest suspends the engine mid-frame, migrates, hands
the terminal descriptor to the destination, and requires the full 128-byte
frame.  QEMU announces a migrated NIC with broadcast frames, so the test
skips wire frames of any other length rather than assuming the transmitted
frame is first.  Removing the subsection makes the destination emit nothing
of the expected length, and the test fails at its readable-socket wait.

Three-configuration gates after the fix: 155/155 normal, 154/154
dependency-minimal, 154/154 ASan/UBSan, and the NPCM GMAC suite 7/7.

### Current XTheadVector gather-immediate and compress checkpoint

An eighth payload, ``tests/tcg/riscv64/test-xtheadvector-compress.S``, closes
the last two permutation forms.  All ten now have dynamic coverage at RV64
e8,m1.

``th.vrgather.vi`` is checked with a valid index, an out-of-range index, a
masked body and a nonzero ``vstart`` through the helper path, then with vl
equal to VLMAX so the translator takes its optimised broadcast path instead.
An immediate of exactly VLMAX is included deliberately: the far out-of-range
case cannot separate the translator's ``>=`` test from a ``>`` test.

``th.vcompress.vm`` is the only permutation form whose written length is
decided by its mask rather than by vl.  The payload checks an alternating
mask, an all-ones mask and an all-zero mask, confirming that the clear starts
at the compacted count rather than at vl, and that a zero mask leaves nothing
of the previous destination.  Its legality cases cover the destination
overlapping either source and the zero-``vstart`` requirement.

Again no defect was found, and again the payload is mutation-sensitive.
Clearing from ``vl`` instead of the compacted count fails at exit 5, removing
``s->vstart_eq_zero`` from ``vcompress_vm_check_th`` fails at exit 24, and
relaxing the gather-immediate range test to ``>`` fails at exit 10.  All
mutations were applied to a scratch copy and reverted.

Three e64,m1 compress cases extend the payload to the widest element width.
Their value is specifically the mask layout: ``mlen`` is ``SEW/LMUL``, so the
mask bit for element ``i`` sits at bit ``i*8`` at e8,m1 but at bit ``i*64`` at
e64,m1.  Pinning the compress helper's ``th_elem_mask`` argument to a literal
8 is a no-op at e8 and fails the e64 case at exit 12.

Two other candidate width mutations were tried and rejected as
non-discriminating, which bounds how much further width expansion is worth
here.  Dropping ``sizeof(ETYPE)`` from the compress clear length changes
nothing at e8, and at e64 it overruns the destination register into its
neighbour without altering the checked result; ASan does not report it either,
because the write stays inside ``CPURISCVState`` rather than crossing a
redzone.  The helpers are otherwise uniformly templated and ``H8`` is the
identity on little-endian, so element width alone exercises no further
distinct logic.  The genuine width risk in this area is an index type rather
than an element type, which is what ``UQ-L014`` was.

### Current focused FXCR/MMC-alias checkpoint

Normal and dependency-minimal builds both pass the C910 FXCR execution guest,
the C910 CSR/system-reset qtest, the generated-DT direct-boot contract, the
whole-machine migration qtest, and the portable Linux single- and four-hart
eMMC functional tests.  The ASan/UBSan build passes the FXCR guest and its
complete available board/CSR gates.  The Linux tests use
``root=PARTUUID=1520a110-01``; both writer and fresh verifier reproduce
``fb5ac1fab9c5b0e2b328bbe2149dee4664cf064618884f78c10e5c3bf6a05cda``
and remount root read-only.  The four-hart test additionally requires CPUs 0-3
online in both QEMU processes.

The workspace-local functional transcripts are
``build-beaglev-ahead/tests/functional/riscv64/test_beaglev_ahead.BeagleVAhead.test_emmc_root/console.log``,
``build-beaglev-ahead/tests/functional/riscv64/test_beaglev_ahead.BeagleVAhead.test_emmc_root_smp/console.log``
and the corresponding two paths under ``build-minimal``.  They are build
artifacts, not committed evidence.

The preserved
``../validation-artifacts/fxcr-mutation-20260825/qemu-system-riscv64-no-event-guard``
binary (SHA-256
``8d88b261ae8b944ee231a906d40f6ebff3185fe301806f72a0988955f9cec923``)
removes the event-tracking guard from ``can_use_fpu()``.  It fails the expanded
FXCR firmware at stage 45, the repeated already-sticky NX case, while the
correct binary passes.  Independently removing the raised-event arm from
``riscv_cpu_check_fflags()`` fails at stage 46: quiet-NaN ``flt.s`` raises an
already-sticky invalid event and must dirty FS.  The correct build passes all
48 stages in normal, dependency-minimal and ASan/UBSan configurations.  The
second mutant was restored immediately; no preserved binary or immutable log
is claimed for it.  The generic SoftFloat quick suite passes 17/17, and the
slow ``fp-test-mulAdd`` FMA test passes for f16, f32, f64 and f128.

### Current focused XTheadVector permutation/slide checkpoint

All six XTheadVector firmware payloads run directly on the Ahead machine and
pass **6/6** in each of the normal, dependency-minimal and ASan/UBSan builds.
The instrumented run reports only the established incomplete
``makecontext``/``swapcontext`` support warning, with no ASan/UBSan diagnostic.
The pre-fix helper fails with exit 3; isolated gather-truncation and slide
boundary mutations fail with exits 9 and 13.  The fixed source was restored
before the passing runs.  Checkpoint ``78ad4d6e56`` augments that same sixth
payload with an e64,m1 ``th.vrgather.vv`` oracle and the three LMUL=1
slide-down masked-``vd=v0`` traps.  Isolated vector-index and checker mutations
fail at exits 14 and 15; the fixed source passes in all three builds.

### Current GMAC RIWT and Linux-traffic checkpoint

The focused receive-watchdog and watchdog-migration qtests pass **1/1** each,
the complete GMAC group passes **12/12**, and whole-machine plus legacy-device
migration remain green.  The timer checks low-byte masking, the exact
81,920 ns deadline for ``0xa0`` at the fixed 500 MHz TH1520 reset/reference
rate, RI/NIS/PLIC delivery, reset/zero/immediate-RI cancellation and a
half-expired current-v2 deadline.  The reusable property's default-zero clock
and NPCM's version-1 format are unchanged.

Four clean pinned-Linux runs pass DHCP, 3/3 gateway pings and the SHA-256 of a
1 MiB HTTP download: normal one-/four-hart ``run-9uieaud1`` and
``run-tk0021lq``, plus minimal ``run-ev_lpw_x`` and ``run-xc7n_lma``.  Retained
``run-53c1sfu1`` is a contention/timing-sensitive Linux masked-RI race: RIWT
expires while RIE is masked, an unrelated TX interrupt W1C-clears RI without
scheduling RX, and three completed descriptors are temporarily stranded.  It
is not hidden by a QEMU workaround.

The current complete board gates pass **116/116** normal, **115/115**
dependency-minimal and **115/115** ASan/UBSan.  The normal full RISC-V TCG gate
passes **37/37**, the enumerated Ahead-specific minimal TCG gate passes
**14/14**, and the six vector payloads pass directly under ASan/UBSan.

The complete aggregate checkpoints below predate the scalar-permutation
payload and are retained as historical evidence rather than silently
incremented.  The current complete results are stated above.

### Pre-scalar-permutation complete normal build

* `build-beaglev-ahead/tests/qtest/beaglev-ahead-test -q`: **113/113**.
* `build-npcm/tests/qtest/npcm_gmac-test`: **7/7**; its added normal-descriptor
  case is a bounded shared-model compatibility check, not a claim of complete
  NPCM TX-offload coverage.
* `build-beaglev-ahead/tests/qtest/riscv-csr-test -q`: **11/11**.
* Local TCG payloads: **17/17** — XTheadVector smoke/state/overlap/FP/
  reduction, standard RVV widening legality, C910 MM/priority/MAEE/
  physical-PMA/PMU/scalar legality/XTheadBa-off, Zicclsm on/off, and C900
  CLINT/PLIC.  The complete normal RISC-V softmmu TCG suite passes **29/29**.
* `git diff --check`: clean.

### Pre-scalar-permutation complete dependency-minimal build

* `build-minimal/tests/qtest/beaglev-ahead-test -q`: **112/112**.
* `build-minimal/tests/qtest/riscv-csr-test -q`: **4/4**.
* C910 scalar-legality and XTheadBa-off payloads: **2/2**.
* XTheadVector smoke/state/overlap/FP/reduction payloads run directly with
  `-M beaglev-ahead -bios`: **5/5**.  This is the preceding complete
  checkpoint; the current focused six-payload result is recorded above.

### Current and historical ASan/UBSan checkpoints

The preceding complete dependency-minimal sanitizer checkpoint passed:

* `build-beaglev-ahead-sanitize/tests/qtest/beaglev-ahead-test -q`:
  **112/112**.
* `build-beaglev-ahead-sanitize/tests/qtest/riscv-csr-test -q`: **4/4**.

The current focused sanitizer checkpoint passes the 48-stage C910 FXCR
execution guest and the XTheadVector
smoke/state/overlap/FP/reduction/scalar-permutation payloads **6/6**.  ASan
emits its expected warning about incomplete ``makecontext``/``swapcontext``
support, with no ASan/UBSan finding.

At the preceding storage/PLL checkpoint, the rebuilt sanitizer binary passed
the complete AP-clock/CPR group **9/9** and the DWC MSHC/storage group
**13/13**, including the PLL poll and
eMMC HS400 tests, with no ASan/UBSan finding.  Under instrumentation the Linux
functional run had not completed after 180 seconds and emitted guest
soft-lockup warnings.  No ASan/UBSan diagnostic appeared; the run is
inconclusive and remains a non-pass whose cause is unassigned.

The following complete-board result is the earlier dependency-minimal
sanitizer checkpoint; it predates four sanitizer-available board additions
(the three storage tests and PLL poll test) and is retained
as historical evidence rather than relabeled as current:

* `build-sanitize/tests/qtest/beaglev-ahead-test -q`: **108/108**.
* `build-sanitize/tests/qtest/riscv-csr-test -q`: **4/4** (C910 CSR and the
  active/inhibited/pending PMU migration cases present in this build).
* C910 scalar-legality and XTheadBa-off payloads: **2/2**, with no ASan/UBSan
  finding.
* Machine-specific semihosted payloads: **9/9** — C910 MM/priority/MAEE/
  physical-PMA, C900 CLINT/PLIC, four-hart SMP, DW UART, and DW timer.
* XTheadVector smoke/state/overlap/FP/reduction payloads run directly with
  `-M beaglev-ahead -bios`: **5/5**.  This retained historical result predates
  the scalar-permutation payload; the current focused sanitizer result is
  **6/6** above.
* The generic `test-thead-c910-pmu` and `test-rvv-widen-illegal` invocations
  are not counted in this sanitizer result: their normal harness requires the
  `virt` machine and a generic `rv64` CPU, which this intentionally
  dependency-minimal sanitizer binary does not build.  The normal suite and
  the sanitizer CSR/board tests cover their branch-relevant paths.

The current complete sanitizer gate and retained historical evidence show no
sanitizer finding in the covered paths.  These results do not establish
conformance to the physical TH1520; the open ledger remains authoritative for
that distinction.

### Portable Linux eMMC-root gate

The functional test pins Linux 6.11.9
(``174f8bb87f08961e54fa3fcd954a8e31f4645f6d6af4dd43983d5e9841490fb0``)
and groeck's RISC-V ext2 rootfs at commit
``9819da19e6eef291686fdd7b029ea00e764dc62f``
(``b6ed95610310b7956f9bf20c4c9c0c05fea647900df441da9dfe767d24e8b28b``).
The functional test embeds the raw ext2 filesystem at sector 2048 in a
deterministic MBR wrapper with disk signature ``0x1520a110`` and boots
partition 1 as ``root=PARTUUID=1520a110-01``.  This makes root discovery
independent of asynchronous Linux MMC probe order.  The generated DT adopts
the RevyOS/vendor-kernel mapping from commit
``a092d55649279e1c9bcda2769b8f6b4370fa2c94``:
``mmc0=&emmc``, ``mmc1=&sdio0`` and ``mmc2=&sdio1``.  The direct-boot FDT qtest
checks all three aliases.  Pinned upstream commit
``2709dd5ae32f0828f386327c76bba9f39f63a1c6`` omits MMC aliases, so the QEMU
mapping remains a vendor-compatibility choice pending official-stock-DTB and
hardware evidence.

With ``maxcpus=1``, Linux enumerates HS400 as ``mmc0``, mounts the eMMC-backed
``mmcblk0p1`` ext2 root, enters a controlled root shell, writes and syncs a
deterministic 1 MiB payload, verifies its SHA-256, and remounts read-only.
After QMP closes that QEMU process, a new process reopens the same scratch
image, verifies the hash again and remounts read-only.  The deterministic
payload SHA-256 is
``fb5ac1fab9c5b0e2b328bbe2149dee4664cf064618884f78c10e5c3bf6a05cda``.
Normal and dependency-minimal builds pass.

This proves a single-hart root mount plus one clean sync/remount/process-reopen
path.  It is not an official-image, normal-distro-init, host-cache eviction,
power-loss durability, ``e2fsck`` or storage-stress result.

### Portable Linux four-hart audit

One exploratory Linux 6.11.9 run brought up only three CPUs and then reported
a CPU0 soft lockup in ``smp_call_function_many_cond`` during the unaligned-
access check.  Its raw log remains workspace-local as
``../validation-artifacts/beaglev-ahead-linux611-smp-failure-20260825.log``.
Eight later completed four-hart runs in
``../validation-artifacts/smp-diagnostics/`` all reported
``smp: Brought up 1 node, 4 CPUs`` without that offline/lockup sequence.  A
partial run labeled ``tcg-single1`` also reached four CPUs but ended before the
MMC probe, so it is not a single-threaded-TCG storage result.  No QEMU argv
manifest was preserved for these exploratory runs; acceleration and affinity
settings inferred from filenames are not treated as proved configuration.

Seven completed four-hart runs used ``root=179:0 rootwait``.  One mounted the
root; one enumerated HS400 and ``mmcblk1`` before root open failed; four
reported ``Failed to initialize a non-removable card``; and one reached root
open without a final card result.  A three-hart numeric-root control also
failed.  This exploratory matrix does not isolate a four-hart SDHCI defect.
Linux can parse a numeric major/minor into a nonzero root device before the
block device exists, at which point its root-wait path can return while
asynchronous MMC discovery
is still in flight.  That explains the early root open/panic race, but does not
by itself explain the card-initialization failures; intermittent storage-model
behavior was therefore still possible at that audit point.

A fresh qcow2 overlay backed by the pinned ext2 image was then booted, before
the alias/PARTUUID change, with ``root=/dev/mmcblk1 rootwait``.  It brought up
all four CPUs, enumerated HS400, mounted ext2 and emitted
``FOUR_HART_EMMC_PASS``; the raw evidence is
``../validation-artifacts/smp-diagnostics/path-root-fourhart.log``.  This one
path-based pass was a successful control, not by itself a repeated SMP gate.

The later formal four-hart test uses a fresh qcow2 overlay backed by the
deterministic MBR image and ``PARTUUID=1520a110-01``.  It rejects CPU-offline,
soft-lockup, card-initialization, I/O, root-open and panic patterns.  Both its
writer and a fresh QEMU verifier require CPUs 0-3 online, enumerate HS400,
mount ext2, reproduce the stable payload hash and remount root read-only.
Normal and dependency-minimal builds pass.  This supersedes the path-only
control, but it is still bounded evidence rather than indefinite stress or a
statement about the owner's hardware.

### C910 FXCR implementation checkpoint

Pinned openC910 RTL commit
``b91c90914c19f114d35c8f6b73408eb241ed847c`` defines user read/write CSR
``FXCR`` at 0x800.  Access is illegal with FS Off and a legal write makes FS
Dirty.  Reset is zero and the visible mask is ``0x0780003f``: bits 26:24 alias
``frm``; bit 23 is DQNaN, where zero selects the default/canonical NaN and one
propagates a source NaN; bit 5 is writable and otherwise sticky-sets on an FP
exception event; and bits 4:0 alias ``fflags``.  Direct software writes to
``fflags`` do not by themselves prove that an exception event occurred.

The current local checkpoint replaces the former zero-valued FXCR placeholder
with those fields and aliases, drives SoftFloat NaN selection, tracks exception
events separately from architectural ``fflags``, marks FS Dirty on writes and
adds reset handling plus version-3 migration fields and old-stream defaults.
Normal, dependency-minimal and ASan/UBSan gates pass.  The execution firmware
checks M/S/U FS-Off traps and enabled access, read-versus-write FS transitions,
the exact mask/aliases, scalar H/S/D and XTheadVector canonical-versus-source
NaNs, every IEEE exception class, and direct architectural-flag writes.  It
also checks a new occurrence of already-sticky NX and an integer-result,
quiet-NaN ``flt.s`` whose already-sticky invalid event changes only FE but must
change FS from Clean to Dirty.  The CSR qtest verifies aliases and that
emulated warm/system reset clears FXCR and FS.  Its dedicated current-stream
migration guest stops the source with ``DQNaN|NX``, resumes the destination
without first reading FXCR, proves quiet-NaN payload propagation and requires
a new already-sticky NX event to set FE.  Retained RAM then selects a
post-system-reset phase which proves FS Off, executes the first
exception-producing ``flt.s`` and requires FS Dirty before its first FXCR
read.  A qtest-only hook also injects a local invalid SoftFloat event into the
stopped incoming destination; the first resumed FXCR read must still be exactly
``DQNaN|NX``, and a mutation removing the post-load raised-event clear fails at
``0xdead3008``.  The current normal build passes all 14
``riscv-csr-test`` cases with the extended execution gate; focused
dependency-minimal and ASan/UBSan reruns of the added hook pass too.  The
instrumented run emits only its established ``makecontext`` warning, with no
ASan or UBSan finding.

The stage-45 fast-path mutant and independent stage-46 raised-event mutation
are recorded in the focused audit evidence above.  The SoftFloat quick suite
passes 17/17 and the slow mulAdd/FMA test passes.  An actual pre-version-3
FXCR-bearing migration stream and physical capture remain open.  ``CPU-016``
keeps the owner's unidentified TH1520 stepping separate from this pinned-RTL
QEMU contract.

## Open local fidelity gaps (not confirmed bugs)

The following are deliberately retained as open work.  A future implementation
change must add a reproducer and a regression before changing any of them.

* `CPU-004`, `CPU-005`, `CPU-006`, `CPU-008`, `CPU-010`, and `CPU-015` in the
  [hardware ledger](beaglev-ahead-hardware-validation.md): exact physical PMA
  ranges, C910 scalar/XTheadVector stepping behavior, PMU event semantics,
  CMO privilege, and alignment/vector exception behavior still need silicon
  comparison.
* The four-hart event detailed above is not reproduced in eight later complete
  exploratory boots or the formal PARTUUID-based normal/minimal integrity and
  process-reopen gates.  Keep it as a local historical observation until
  longer stress and a sufficiently long single-threaded-TCG control increase
  confidence; do not reuse the numeric-root harness failures as a QEMU
  reproducer or hardware fact.
* `CPU-016`: the focused FXCR gate is complete in normal, dependency-minimal
  and ASan/UBSan builds, but is grounded in pinned openC910 RTL, not the
  owner's unidentified TH1520 stepping.  Preserve that distinction until an
  old-version FXCR-bearing stream and the physical-hart matrix are complete.
* Runtime UART0 remains deferred for an unresolved reason in the portable
  Linux 6.11.9/generated-DT combination; pinctrl/fw-devlink involvement is
  suspected but not established.  Earlycon works, and other pinned-kernel and
  external-DT control runs bind ``ttyS0``.  Stock init waits for absent
  ``eth0`` and subsequently reports network-interface and TPM selftest
  failures before printing ``Boot successful.``  These configuration-specific
  observations are recorded, not treated as board-model regressions.
* `MIG-001`: a six-descriptor SDIO0 v4-ADMA read now migrates at the
  controller's real asynchronous boundary: five descriptors are complete and
  the sixth is armed for the existing 100 ns SDHCI transfer timer.  The
  destination keeps the sixth buffer untouched through 99 ns and completes it
  one nanosecond later; removing the timer from SDHCI VMState makes the
  regression fail.  It uses separately initialized identical backing images,
  so it validates controller/timer ownership rather than migration of card
  media or a backend.  A separate USB-host qtest migrates a pending xHCI
  No-Op completion, consumes it, then completes the next command at the next
  event-ring slot; omitting xHCI's command-ring VMState makes it fail.  That
  proves controller ring and IRQ continuity, not an attached USB device or
  endpoint transfer.  GMAC transmit now migrates at its real in-flight
  boundary: a frame suspended between its first and terminal segment carries
  its accumulated bytes and checksum control through a subsection, and the
  destination completes it once the guest hands over the last descriptor.
  USB migration during an attached transfer, and GMAC receive during an
  in-flight frame, still need phase/ownership tests.  Focused same-version
  GMAC coverage preserves MAC0/MAC31, frame-filter, address-hash and VLAN
  registers, IPC state and an active enhanced ring, then proves post-load
  reject/accept behavior and a Type-2 RDES4 result.  DWC GMAC VMState v2 now
  also preserves an armed RIWT deadline; pre-v2 loads remain unarmed because
  no old deadline exists.  The tests create the destination socket separately
  and do not migrate queued packets or the backend.  AXI DMAC,
  I2C, SPI, and PVT are intentionally synchronous today;
  adding asynchronous timing requires versioned VMState and boundary tests.
* `GMAC-001`: the receive-filter model follows the current DT contract of 64
  hash bins and 32 total perfect addresses, but no physical capability dump
  proves that synthesis.  VLAN-hash mode remains disabled.  Type-2 descriptor
  status is now modeled for a bounded documented subset, but checksum-error
  drop/forward threshold behavior still requires owner-board capture.  TX
  checksum insertion now has a bounded CIC0-3 contract, but the exact physical
  feature word, FIFO/PBL and threshold recovery, CIC2 seed and CIC1-on-IPv6
  semantics, normal/enhanced status, malformed/trailing/padding behavior,
  fragments and IPv6 extensions, stacked/alternate VLANs, UDP-zero behavior,
  PTP composition, descriptor/bus-fault ordering and sustained traffic remain
  open.  RIWT uses a fixed 500 MHz TH1520 reset/reference-rate assumption; the
  owner's actual clock, repeated-DIC and W1C/re-arm ordering, live writes, DMA
  stop/disable, dynamic gate/rate changes, reset domains and expiry latency are
  unverified.  The retained masked-RI Linux race keeps contention stress open
  without changing the device model.  The previous generic helper's incidental
  S-VLAN/QinQ traversal is not
  claimed by the new one-tag/ESVL parser.  Exact filter, control-frame, pause
  and VLAN behavior also requires physical comparison.
* `USB-002`: the current model is a host-only digital DWC3/xHCI integration
  with synthetic capability values.  PHY/link timing, device/OTG role,
  ID/VBUS, suspend/resume, and reset-domain independence are not modeled.
* The PVT model exposes deterministic readings but not conversion latency,
  alarms, timer state, or interrupt aggregation; these are tracked as
  `PVT-001`, not as regressions in an upstream device.
* The watchdog and RTC integrations use documented or explicitly provisional
  synthesis/reset values.  Exact component IDs, clock-gate phase, retention,
  and board reset behavior remain under `WDT-001` and `RTC-001`.
* Display/GPU, media, NPU, Wi-Fi/Bluetooth, auxiliary C906/E902/DSP cores,
  security/firewall behavior, and board buttons remain outside the current
  software-visible contract.  Their status is recorded in the hardware
  ledger; no placeholder is presented as complete emulation.

## Promotion rule for a new local bug

Treat a suspected issue as a local bug only when all of the following are
available:

1. a minimal guest or qtest reproducer that fails on this branch;
2. a stated expected result from a public specification, a pinned source
   trace, or a repeatable owner-board capture;
3. a normal-build and sanitizer result that isolates the failure from the
   harness/configuration; and
4. a regression test and a source/documentation fix, or an explicit blocker
   explaining why it cannot yet be fixed.

Until then, add the observation to the appropriate hardware-ledger item and
keep it out of the upstream report tally.
