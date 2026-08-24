# BeagleV Ahead local implementation audit

This document is the local counterpart to
[the upstream QEMU bug handoff](beaglev-ahead-upstream-bugs.md).  It covers
defects and limitations introduced by the BeagleV Ahead/TH1520 board and C910
work in this branch.  These findings are not claims about pre-existing
upstream QEMU, and must not be filed as upstream bugs without an independent
baseline reproducer.

* Audit checkpoint: 2026-08-24
* Branch: `beaglev-ahead`
* Pre-fix workspace HEAD: `81058b22df`
* Pinned upstream comparison: `bde2492aace2b5acb755a5b057013e915163a77f`

## Current disposition

The nine local findings recorded during implementation (`UQ-L001` through
`UQ-L009`) are fixed and have focused regressions.  The latest audit found
that `th.vmfirst.m` accepted a nonzero `vstart`; the fail-before guest reached
stage 11 because the instruction executed instead of trapping.  The focused
normal, dependency-minimal and sanitizer runs pass after the translator fix.
Remaining items are fidelity gaps or hardware questions, not confirmed bugs;
they stay open until an authoritative specification or an owner-board capture
establishes the expected behavior.

The detailed historical reproducer and fix records remain in the `UQ-L*`
sections of [the upstream handoff](beaglev-ahead-upstream-bugs.md).  This file
is the short review index and current test checkpoint.

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

## Audit evidence

All commands below were run from the QEMU source directory.  Qtest commands
use the locally built QEMU through `QTEST_QEMU_BINARY`; sanitizer runs set
`ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer cannot operate reliably
under the qtest parent/ptrace setup.  The ASan `makecontext` warning is emitted
by the runtime and was not accompanied by an ASan or UBSan finding.

### Normal build

* `build-beaglev-ahead/tests/qtest/beaglev-ahead-test -q`: **100/100**.
* `build-beaglev-ahead/tests/qtest/riscv-csr-test -q`: **11/11**.
* Local TCG payloads: **14/14** — XTheadVector smoke/state/FP/reduction,
  standard RVV widening legality, C910 MM/priority/MAEE/physical-PMA/PMU,
  Zicclsm on/off, and C900 CLINT/PLIC.
  The state payload includes the `th.vmfirst.m` nonzero-`vstart` regression.
* `git diff --check`: clean.

### Dependency-minimal sanitizer build

* `build-sanitize/tests/qtest/beaglev-ahead-test -q`: **99/99**.
* `build-sanitize/tests/qtest/riscv-csr-test -q`: **4/4** (C910 CSR and the
  active/inhibited/pending PMU migration cases present in this build).
* Machine-specific semihosted payloads: **9/9** — C910 MM/priority/MAEE/
  physical-PMA, C900 CLINT/PLIC, four-hart SMP, DW UART, and DW timer.
* XTheadVector smoke/state/FP/reduction payloads run directly with
  `-M beaglev-ahead -bios`: **4/4**.
  The extended state payload also passes the dependency-minimal build.
* The generic `test-thead-c910-pmu` and `test-rvv-widen-illegal` invocations
  are not counted in this sanitizer result: their normal harness requires the
  `virt` machine and a generic `rv64` CPU, which this intentionally
  dependency-minimal sanitizer binary does not build.  The normal suite and
  the sanitizer CSR/board tests cover their branch-relevant paths.

These results show that the known local fixes are stable under normal and
sanitized execution.  They do not establish conformance to the physical
TH1520; the open ledger remains authoritative for that distinction.

## Open local fidelity gaps (not confirmed bugs)

The following are deliberately retained as open work.  A future implementation
change must add a reproducer and a regression before changing any of them.

* `CPU-004`, `CPU-005`, `CPU-006`, `CPU-008`, and `CPU-010` in the
  [hardware ledger](beaglev-ahead-hardware-validation.md): exact physical PMA
  ranges, C910/XTheadVector stepping behavior, PMU event semantics, and
  alignment/vector exception behavior still need silicon comparison.
* `MIG-001`: storage, GMAC, and USB migration during in-flight DMA or an
  attached transfer still need phase/ownership tests.  DMAC, I2C, SPI, and
  PVT are intentionally synchronous today; adding asynchronous timing requires
  versioned VMState and boundary tests.
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
