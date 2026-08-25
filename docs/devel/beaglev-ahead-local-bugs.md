# BeagleV Ahead local implementation audit

This document is the local counterpart to
[the upstream QEMU bug handoff](beaglev-ahead-upstream-bugs.md).  It covers
defects and limitations introduced by the BeagleV Ahead/TH1520 board and C910
work in this branch.  These findings are not claims about pre-existing
upstream QEMU, and must not be filed as upstream bugs without an independent
baseline reproducer.

* Audit checkpoint: 2026-08-25
* Branch: `beaglev-ahead`
* Latest-finding pre-fix workspace HEAD: `e734a5c4c8`
* Pinned upstream comparison: `bde2492aace2b5acb755a5b057013e915163a77f`

## Current disposition

The thirteen local findings recorded during implementation (`UQ-L001` through
`UQ-L013`) are fixed and have focused regressions.  The GMAC audit found that
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

The latest local checkpoint fixes two RV64 scalar-index wrong-result defects
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

## Audit evidence

All commands below were run from the QEMU source directory.  Qtest commands
use the locally built QEMU through `QTEST_QEMU_BINARY`; sanitizer runs set
`ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer cannot operate reliably
under the qtest parent/ptrace setup.  The ASan `makecontext` warning is emitted
by the runtime and was not accompanied by an ASan or UBSan finding.

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

### Current focused XTheadVector scalar-permutation checkpoint

All six XTheadVector firmware payloads run directly on the Ahead machine and
pass **6/6** in each of the normal, dependency-minimal and ASan/UBSan builds.
The instrumented run reports only the established incomplete
``makecontext``/``swapcontext`` support warning, with no ASan/UBSan diagnostic.
The pre-fix helper fails with exit 3; isolated gather-truncation and slide
boundary mutations fail with exits 9 and 13.  The fixed source was restored
before the passing runs.

The complete aggregate checkpoints below predate the scalar-permutation
payload and are retained as historical evidence rather than silently
incremented.  No post-``9eef55f462`` **30/30** complete normal TCG-suite result
is claimed here.

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
emulated warm/system reset clears FXCR and FS.  Whole-machine migration reads
back the exact stored FXCR/FRM/FFLAGS fields, but does not yet execute an FP
operation after load.

The stage-45 fast-path mutant and independent stage-46 raised-event mutation
are recorded in the focused audit evidence above.  The SoftFloat quick suite
passes 17/17 and the slow mulAdd/FMA test passes.  Post-load FP execution, an
actual pre-version-3 migration stream, first exception-producing execution
after emulated reset and physical capture remain open.  ``CPU-016`` keeps the
owner's unidentified TH1520 stepping separate from this pinned-RTL QEMU
contract.

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
  owner's unidentified TH1520 stepping.  Preserve that distinction until
  post-load execution, an old-version migration stream and the physical-hart
  matrix are complete.
* Runtime UART0 remains deferred for an unresolved reason in the portable
  Linux 6.11.9/generated-DT combination; pinctrl/fw-devlink involvement is
  suspected but not established.  Earlycon works, and other pinned-kernel and
  external-DT control runs bind ``ttyS0``.  Stock init waits for absent
  ``eth0`` and subsequently reports network-interface and TPM selftest
  failures before printing ``Boot successful.``  These configuration-specific
  observations are recorded, not treated as board-model regressions.
* `MIG-001`: storage, GMAC, and USB migration during in-flight DMA or an
  attached transfer still need phase/ownership tests.  Focused same-version
  GMAC coverage preserves MAC0/MAC31, frame-filter, address-hash and VLAN
  registers, IPC state and an active enhanced ring, then proves post-load
  reject/accept behavior and a Type-2 RDES4 result.  It creates the destination
  socket separately and does not migrate queued packets or the backend.  DMAC,
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
  open.  The previous generic helper's incidental S-VLAN/QinQ traversal is not
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
