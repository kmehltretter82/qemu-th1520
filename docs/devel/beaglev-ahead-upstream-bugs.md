# Upstream QEMU bug handoff from BeagleV Ahead work

This document records defects in pre-existing upstream QEMU code that were
found while implementing BeagleV Ahead support.  It is a reviewer handoff,
not a claim that an issue has been accepted by QEMU or assigned a CVE.

The audit baseline is QEMU `staging` commit
`2be159078ea26feac4c9c9902acf8906f1a05c2a` fetched on 2026-08-24 and merged
into this workspace by `2d7bb62c70`.  Recheck every item against current
`master` immediately before reporting it.

`UQ-002` and `UQ-012` were additionally rechecked against freshly fetched
QEMU `master` commit
`bde2492aace2b5acb755a5b057013e915163a77f` on 2026-08-24.  Their generic
reproducers fail there and pass on this branch; the remaining pre-filing step
is a human duplicate review and report/patch preparation.

## Snapshot and counting rules

The current conservative tally is:

* **12 proposed new upstream report units** (`UQ-001` through `UQ-012`);
* **1 matching public upstream report** (`UQ-K001`), which is not new;
* **1 matching public upstream patch series** (`UQ-K002`), which is not new;
* **3 additional investigation candidates** (`UQ-C001` through `UQ-C003`);
* **5 defects confined to this not-yet-upstream board/CPU implementation**
  (`UQ-L001` through `UQ-L005`), which must not be reported as existing
  upstream bugs; and
* **0 reports filed by this project so far**.

A source-path audit of the 97 upstream commits between the prior
`bde2492aace2b5acb755a5b057013e915163a77f` snapshot and the baseline above
found changes only in `target/riscv/cpu.c` and `target/riscv/tcg/pmu.c` among
the files implicated by UQ-001 through UQ-010.  The incoming Zicclsm CPU-option
work and `minstret` exception-accounting fixes do not address the zero-ID,
duplicate-event-map or overflow-width findings.  None of the SDHCI, AT24C,
NPCM GMAC, DWC3 or xHCI candidate paths changed in that range.  The
conservative report tally therefore did not change after the staging merge;
this path audit is not a substitute for the required current-master
reproducer and duplicate search.

A subsequent focused CSR audit on that merged baseline found `UQ-011`.  Two
freestanding M-mode payloads independently prove both sides of the defect:
with `Sscofpmf` enabled, a write/read of `mie.LCOFIE` returns zero; with the
extension disabled, writes to `mideleg.LCOFI` and `mip.LCOFIP` incorrectly
stick.  The current conservative tally is therefore 11.  The quick public
tracker and mailing-list search on 2026-08-24 found no matching report, but the
required current-`master` duplicate search remains outstanding.

The subsequent generic PMU migration audit adds `UQ-012`.  Architectural
event selectors and counter bases were present in the stream, but the runtime
event map, fixed-counter clock snapshots and overflow deadline were not
reconstructed, while `mcyclecfg` and `minstretcfg` were omitted entirely.
Follow-up coverage also found that the migration callbacks were skipped when
`pmu-mask=0`, although fixed counters and Smcntrpmf filtering still exist in
that configuration.  Deterministic migration guests now distinguish
configuration loss, source-value loss, destination counter stoppage, missing
overflow rearming, inhibited state and already-pending overflow state.  These
are additional symptoms of the same migration defect, not a thirteenth report
unit.  The current conservative tally is therefore 12.  A focused
GitLab/qemu-devel search on 2026-08-24 found no match; a human duplicate search
is still required.

The sanitizer follow-up found that each RISC-V TCG CPU initialization replaced
two process-global user-option hash-table pointers without releasing their old
tables.  A four-hart BeagleV Ahead process leaked 960 bytes in 18 allocations.
Exact current `master` contains the assignments, but a public June 2026 v3
no-TCG build series already carries the broader equivalent lifetime fix.  This
is recorded as `UQ-K002`; local commit `79dd49c99b` is a backport, and the
conservative new-report tally does not change.

The 2026-08-24 MR75203 PVT milestone did not add a report unit.  Its missing
alarm, timer, conversion-latency and interrupt behavior is new-model scope,
not a defect in pre-existing upstream QEMU.

The 2026-08-24 DesignWare watchdog milestone likewise did not add a report
unit.  Upstream had no reusable DW APB watchdog model to regress, and the
TH1520 integration, provisional synthesis values and hardware uncertainties
are new-board scope rather than defects in pre-existing QEMU code.

The 2026-08-24 X-Gene/TH1520 RTC milestone also did not add a report unit.
Upstream had no X-Gene RTC device model, so the new register/timer/migration
implementation has no pre-existing QEMU regression to report.  Mainline
Linux's missing TH1520 node and 32.768 kHz prescaler setup are Linux
integration gaps, not QEMU bugs.  The conservative QEMU tally did not change.

The 2026-08-24 board-LED milestone did not add a QEMU report unit either.
Upstream's generic LED device already accepts GPIO input and migrates its
intensity; exposing that intensity read-only through QOM and wiring board
consumers are feature work.  An older vendor Linux DTS calls the five user
LEDs green, while the schematic, BOM and pinned mainline DTS identify them as
blue.  That stale cross-project label is recorded under ``BOARD-002`` and is
not a defect in pre-existing QEMU.  The conservative QEMU tally did not change.

The 2026-08-24 reset-coupling milestone also did not add a QEMU report unit.
It connects Linux-described TH1520 AP and storage reset groups to devices
introduced or integrated on this branch.  The collapsed whole-device reset
scope and remaining hardware unknowns are new-board fidelity work under
``RST-001``, not defects in QEMU's pre-existing machines or reusable devices.
The conservative QEMU tally did not change.

The 2026-08-24 clock-gate milestone did not add a QEMU report unit.  Exporting
TH1520 leaf-gate state, wiring new SoC clock links and defining provisional
pause/resume behavior for the branch's PWM, timer and watchdog integrations
are new-board fidelity work under ``CLK-002``.  Focused normal and migration
tests found no independent defect in a pre-existing upstream machine.  The
conservative QEMU tally did not change.

The 2026-08-24 C910 MXSTATUS.MM milestone did not add a report unit.  It found
two real implementation defects: the branch's new C910 definition exposed
Zfh while losing its Zfhmin dependency, and its imported XTheadVector helpers
accepted misaligned loads/stores.  Because neither the C910 model nor that
XTheadVector implementation exists at the upstream baseline, both are local
patch-series defects (`UQ-L002` and `UQ-L003`) rather than bugs in released
upstream QEMU.  The generic scalar Zicclsm implementation remains independent
and passes its enabled/disabled tests.

The guarded-page continuation of that milestone added `UQ-011` while making
the C910 PMU test deterministic with instruction counting.  `UQ-011` is a
generic upstream Sscofpmf CSR-mask defect exposed by an independent `rv64`
payload, not a defect in the new C910 model.  The host-tick overflow scheduling
observation remains candidate `UQ-C003`; it is not included in the tally.

The following C910 MAEE milestone did not add an upstream report unit.  The
strong-order PTE behavior, page-dependent scalar alignment and C910-specific
instruction access fault are new CPU-model work absent from the upstream
baseline.  Moving RISC-V from the legacy TLB-fill callback to QEMU's existing
alignment-aware callback is required to express that behavior on a first TLB
miss; focused C910 and generic Zicclsm regressions found no independent defect
in an existing upstream CPU.  The proposed-report tally remains 11.

The MAEE atomic/vector continuation likewise leaves the proposed-report tally
at 11.  It did uncover `UQ-L004`: ratified RVV translation accepted an
overlapping vector-store encoding before the branch's XTheadVector decoder.
The generic decoder's missing explicit Zve32x gate is concrete and fixed, but
the upstream baseline forces VILL when no standard vector extension exists;
only this branch's not-yet-upstream XTheadVector state made the collision
reachable.  It is therefore a local patch-series defect, not a defect that can
be reproduced on released upstream QEMU.  Parking secondary harts in the
three board payloads also fixed test-data races; that is test hygiene rather
than another emulator report unit.

The expanded MAEE width/form audit leaves the proposed-report tally at 11 and
adds local finding `UQ-L005`.  The branch had treated PTE[63:59] as standard
reserved bits whenever MXSTATUS.MAEE was clear.  Pinned openC910 RTL instead
shows that C910 always owns those bits: it uses them with MAEE set and ignores
them in favor of synthesis-specific physical-system-map attributes otherwise.
The upstream baseline has neither a C910 CPU nor XTheadMaee, so this cannot be
reported as a regression in released upstream QEMU.  The missing TH1520
physical PMA ranges are fidelity scope and remain in ledger item `CPU-004`,
not a sixth local implementation defect.

The 2026-08-24 USB-host milestone added two report units.  Unlike the new
TH1520 wrappers, `UQ-009` and `UQ-010` affect the pre-existing generic DWC3 and
xHCI models and were independently exposed by an upstream Linux host driver.

A report unit groups symptoms that have the same subsystem, reproducer, and
likely fix.  A maintainer may reasonably split `UQ-003`, `UQ-004`, `UQ-007`,
or the adjacent 64-bit-access audit from `UQ-010`, so the eventual issue count
can increase without discovering a new underlying problem.

`REPORTABLE` below means the source defect is sufficiently concrete to merit
an upstream report after the listed isolation and duplicate-search steps.  It
does not mean those final submission steps have already happened.

## Required upstream procedure

Before filing any item:

1. Reproduce it on current upstream `master` with a direct QEMU command line.
2. Reduce the branch test to the smallest machine-independent qtest or TCG
   payload practical, and prove that it fails before the fix and passes after
   it.
3. Search both the QEMU GitLab tracker and qemu-devel archives again.  The
   web search used for this snapshot is not an exhaustive duplicate search.
4. State at the beginning of the report that an AI/LLM coding agent assisted
   discovery and analysis, followed by human review.  QEMU's current
   [bug-report guidance](https://www.qemu.org/contribute/report-a-bug/)
   explicitly requires disclosure and human triage of automated findings.
5. Send fixes as patches to qemu-devel, not as GitLab merge requests, following
   QEMU's [patch-submission guide](https://www.qemu.org/docs/master/devel/submitting-a-patch.html).
6. For any possible host-memory-safety or denial-of-service issue, initially
   create a **confidential** GitLab work item and follow QEMU's
   [security process](https://www.qemu.org/contribute/security-process/).

Do not attach proprietary manuals, private board data, credentials, serial
numbers, or firmware.  Use only redistributable tests and the public evidence
already cited in the BeagleV Ahead hardware-validation ledger.

## Proposed new reports

### UQ-001: static RISC-V CPU definitions cannot preserve a zero `marchid`

Status: **REPORTABLE; high-confidence generic API defect; downstream reproducer**

Affected upstream code:

* `target/riscv/cpu.c`, `riscv_cpu_cfg_merge()` and `riscv_cpu_init()`;
* `target/riscv/cpu_cfg_fields.h.inc`; and
* any static CPU definition that needs an architecturally valid zero ID.

Upstream initializes every RISC-V CPU's `marchid` to QEMU's allocated value
42.  The typed configuration merge treats zero as “unspecified”, so a static
CPU definition cannot override that default with the architecturally valid
value zero.  Consequently the branch's new static `thead-c910` definition
would report `marchid == 42` instead of the C910 value zero.  The baseline does
not yet contain a C910 CPU definition; the new downstream definition is the
concrete reproducer for this generic configuration limitation.  `mimpid`
happens to remain zero because the generic default is also zero.

Branch fix and evidence:

* commit `a0fde250fe` (`target/riscv: preserve the C910 zero architecture ID`)
  adds an explicit-ID marker and CSR checks; and
* `tests/qtest/riscv-csr-test.c` and
  `tests/tcg/riscv64/test-xtheadvector.S` require the corrected value.

Upstream isolation task:

* boot a tiny M-mode payload on `-M virt -cpu thead-c910` that prints or exits
  according to `marchid`; expected zero, current upstream result 42; and
* decide whether a per-field validity bitmap is preferable to the branch's
  single `explicit_ids` flag before posting the fix.

No matching public issue was found in the 2026-08-23 quick search.

### UQ-002: the RISC-V PMU event map loses duplicate counter selections

Status: **REPORTABLE; generic fail-before/pass-after reproducer; final human
duplicate review required**

Affected upstream code:

* `target/riscv/tcg/pmu.c`, especially
  `riscv_pmu_update_event_map()` and `riscv_pmu_incr_ctr()`.

The upstream hash table maps an event number to one counter number.  When two
programmable counters select the same supported event, the second mapping is
discarded, although both counters are permitted to count that event.  Updating
or clearing one selection can therefore also leave the other selection
incorrectly represented.

Branch fix and evidence:

* commit `3354b9818d` (`target/riscv: model C910 performance counters`)
  changes each hash value into a counter bitmask and removes only the counter
  being reprogrammed;
* `tests/tcg/riscv64/test-riscv-pmu-duplicates.S` uses the generic `rv64` CPU,
  selects standard instruction event 2 in counters 3 and 4, and requires both
  values to be nonzero and equal after a bounded loop;
* the payload then clears only `mhpmevent3`, zeros both counters, and proves
  counter 3 stays zero while counter 4 continues counting; and
* the branch exits 0 both with deterministic instruction counting and ordinary
  timing, and the complete normal `check-tcg` gate passes with the test enabled.

The exact deterministic invocation is:

```
qemu-system-riscv64 -icount shift=0 -smp 1 \
  -cpu rv64,pmu-mask=0x18 -M virt -display none -semihosting \
  -device loader,file=test-riscv-pmu-duplicates
```

Freshly fetched upstream `master`
`bde2492aace2b5acb755a5b057013e915163a77f` exits 2 because counter 4 remains
zero.  The same revision also exits 2 without `-icount`, while the branch exits
0 in both modes.  The dependency-minimal ASan/UBSan project configuration
intentionally excludes the generic `virt` machine, so this generic test is not
applicable to that gate; the full normal TCG suite supplies the integration
gate and the two timing modes rule out an `-icount`-only observation.

A quick GitLab/qemu-devel web search on 2026-08-24 for
`riscv_pmu_update_event_map`, `pmu_event_ctr_map`, duplicate `mhpmevent`, and
duplicate RISC-V PMU counters found no matching report.  This is not an
exhaustive duplicate search.  Before filing, a human reviewer must repeat that
search, split the generic bitmask fix out of the board series, run the upstream
CI-relevant tests, and disclose the agent-assisted analysis as required above.

### UQ-003: RISC-V PMU overflow scheduling uses unsafe width arithmetic

Status: **REPORTABLE; high source-level confidence; likely split into two fixes**

Affected upstream code:

* `target/riscv/tcg/pmu.c`, `pmu_timer_trigger_irq()` and
  `riscv_pmu_setup_timer()`.

Two independent-looking width defects were encountered in the same path:

* on RV32, upstream casts the address of a `uint64_t` local to
  `target_ulong *` for a 32-bit counter read.  Only half the object is written,
  after which the full object participates in arithmetic, leaving upper bits
  dependent on uninitialized stack data; and
* when `overflow_delta > INT64_MAX`, upstream records the excess but does not
  clamp `overflow_delta` before converting it through signed nanosecond
  arithmetic.  A nominally distant overflow can become a negative or otherwise
  incorrectly scheduled deadline.

Branch fix and evidence:

* commit `3354b9818d` reads low/high halves into `target_ulong` objects,
  composes a `uint64_t` explicitly, clamps each scheduling interval, and
  carries the remaining delay forward; and
* the C910 overflow payload proves the ordinary near-wrap path, not the RV32
  and greater-than-`INT64_MAX` boundaries.

Upstream isolation task:

* create separate RV32 and very-large-delta unit/TCG tests;
* run the RV32 test with ASan, UBSan, and compiler auto-variable initialization
  disabled as well as enabled; and
* ask the RISC-V maintainer whether to submit two patches/reports.  If split,
  increment the report count.

No matching public issue was found in the 2026-08-23 quick search.

### UQ-004: SDHCI migration omits Host Control 2 and IRQ reconstruction

Status: **REPORTABLE; high confidence; branch migration regression passes**

Affected upstream code:

* `hw/sd/sdhci.c`, `sdhci_vmstate`.

Upstream has a live `hostctl2` register but does not serialize it.  State such
as voltage selection, tuning status, UHS mode controls, and newer addressing
selection is therefore lost on migration.  Upstream also restores interrupt
status and enable words without recomputing the external IRQ output, so the
destination line can disagree with the restored registers until another
device access changes it.

Branch fix and evidence:

* commit `67d94d14c9` (`hw/sd: add SDHCI version 4 foundations`) adds a
  backward-compatible Host Control 2 subsection and a post-load IRQ update;
  and
* `/beaglev-ahead/dwcmshc/migration` writes nonzero Host Control 2 state,
  migrates, and verifies it at the destination.

Upstream isolation task:

* move the state-loss check to an existing generic SDHCI machine/test;
* extend it to assert an enabled pending IRQ before and immediately after
  migration; and
* consider separate reports if maintainers want register serialization and
  derived-output reconstruction reviewed independently.

No matching public issue was found in the 2026-08-23 quick search.

### UQ-005: AT24C EEPROM has no migration description

Status: **REPORTABLE; high confidence; branch migration regression passes**

Affected upstream code:

* `hw/nvram/eeprom_at24c.c`, `at24c_eeprom_class_init()`.

The upstream device supplies no `VMStateDescription`.  Its memory contents,
current address, partial address-byte state, and dirty flag are not transferred
with a VM.  A migrated guest can observe reverted EEPROM data and a changed
position in an in-progress I2C transaction.

Branch fix and evidence:

* commit `f60ffe26e3` (`hw/nvram: migrate AT24C EEPROM state`) serializes the
  I2C parent, pointer/protocol state, dirty flag, and variable-size memory; and
* `/beaglev-ahead/dw-i2c/migration` verifies EEPROM contents and protocol state
  together with the controller's queued receive/interrupt state.

Upstream isolation task:

* attach AT24C to an existing upstream I2C test machine, modify one byte and
  leave a nonzero current address, migrate, then read both back; and
* test devices with one- and two-byte addressing and a writable block backend.

The quick search found unrelated AT24C issue 1485, but no migration report.

### UQ-006: NPCM GMAC receive FCS handling reads beyond the host packet

Status: **REPORTABLE SECURITY CANDIDATE; do not file publicly first**

Affected upstream code:

* `hw/net/npcm_gmac.c`, `gmac_receive()` and
  `gmac_rx_transfer_frame_to_buffer()`.

The receive callback points `frame_ptr` at the packet supplied by the QEMU
network backend, then increases `left_frame` by `ETH_FCS_LEN` without allocating
or appending those bytes.  The DMA helper subsequently asks
`dma_memory_write()` to copy the enlarged length from that pointer.  This can
read four bytes past the host packet buffer and expose adjacent host memory to
guest RAM; it also supplies a garbage FCS instead of the Ethernet CRC.

Branch fix and evidence:

* commit `95af4a301b` allocates `len + ETH_FCS_LEN`, computes the CRC32, and
  copies the complete bounded frame; and
* `tests/qtest/npcm_gmac-test.c` adds socket-backed receive coverage that
  verifies packet bytes, exact FCS, descriptor ownership/length, and status.

Required next action:

* reproduce the pre-fix access with current upstream under ASan using the
  smallest NPCM qtest;
* verify ownership and lifetime guarantees of every network backend buffer;
* treat the four-byte disclosure as security-sensitive even if ASan allocator
  padding makes one run silent; and
* create a confidential GitLab work item before any public patch or detailed
  mailing-list discussion, following QEMU's security process.

The 2026-08-23 public search found issue 3202 for a different NPCM GMAC
**transmit** overflow, but no matching receive/FCS report.

### UQ-007: NPCM GMAC migration loses PHY state and can restore a stale IRQ

Status: **REPORTABLE; high confidence; branch migration regression passes**

Affected upstream code:

* `hw/net/npcm_gmac.c`, `vmstate_npcm_gmac`.

Upstream migrates only the MMIO register array.  Guest-written Clause 22 PHY
registers are reset rather than transferred, and the external interrupt output
is not recomputed from migrated DMA status/mask registers.  Both are
guest-visible state.

Branch fix and evidence:

* commit `95af4a301b` bumps the compatible migration version, serializes the
  two-dimensional PHY register array, and updates the IRQ in post-load; and
* the BeagleV Ahead migration test writes a non-default BMCR value and verifies
  it on the destination.  An NPCM-specific IRQ migration assertion is still
  needed.

Upstream isolation task:

* extend `tests/qtest/npcm_gmac-test.c` with source/destination QEMUs;
* migrate a non-default BMCR/advertisement value and an enabled pending DMA
  interrupt; and
* check old-to-new migration compatibility because the existing stream version
  is zero.

No matching public issue was found in the 2026-08-23 quick search.

### UQ-008: NPCM GMAC MDIO does not model an absent PHY correctly

Status: **REPORTABLE; high confidence; focused qtest exists**

Affected upstream code:

* `hw/net/npcm_gmac.c`, `npcm_gmac_reset()` and
  `npcm_gmac_mdio_access()`.

Only PHY address zero is populated, but the remaining zero-initialized PHY
slots read as all-zero register banks instead of the Clause 22 no-response
value `0xffff`.  In addition, the BMCR autonegotiation path writes the resolved
partner state to `phy_regs[0]` rather than the addressed `phy_regs[pa]`.

Branch fix and evidence:

* commit `95af4a301b` initializes absent PHY slots to `0xffff`, uses the
  addressed PHY consistently, and makes the configured PHY address explicit;
  and
* `tests/qtest/npcm_gmac-test.c` verifies the compatibility device's PHY ID at
  address zero and `0xffff` at address one.

Upstream isolation task:

* split the small MDIO correction and qtest out of the much larger DWC GMAC
  refactor; and
* add a write/read test at a nonzero address to expose the hard-coded index in
  the autonegotiation path.

No matching public issue was found in the 2026-08-23 quick search.

### UQ-009: the generic DWC3 host model omits host-initialization DCTL access

Status: **REPORTABLE; high confidence; Linux and qtest regressions exist;
generic isolation and duplicate search still required**

Affected upstream code:

* `hw/usb/hcd-dwc3.c`; and
* `include/hw/usb/hcd-dwc3.h`.

The upstream `usb-dwc3` device maps the xHCI registers and DWC3 global
registers beginning at `0xc100`, but leaves the device-register aperture
beginning at `0xc700` unassigned.  Linux's generic DWC3 core initialization
reads and soft-resets DCTL at `0xc704` even when the requested final role is
host.  On the TH1520 integration this produced a guest load-access fault at
that address before xHCI could probe.

Expected result:

* a generic DWC3 advertised for host use must allow the host driver's required
  DCTL read/write sequence; and
* setting `DCTL.CSFTRST` must eventually clear so initialization can continue.

Baseline result:

* `0xc704` is a decode hole, so Linux faults rather than completing the reset.

Branch fix and evidence:

* commit `38f6addbc8` (`hw/usb: model DWC3 DCTL soft reset`) adds the minimal
  host-required DCTL block, retains
  `RUN_STOP`, completes `CSFTRST` immediately in this untimed host-only model,
  resets it with the core and migrates it through a backward-compatible
  versioned field;
* commit `af9076a779` adds `/riscv64/beaglev-ahead/usb/registers`, which checks
  reset, soft-reset self-clear, readback and system reset, while the USB
  migration test preserves `RUN_STOP`; and
* after this fix, pinned upstream Linux commit
  `2709dd5ae32f0828f386327c76bba9f39f63a1c6` proceeds past DWC3 core reset.

Current branch reproducer:

```sh
QTEST_QEMU_BINARY="$PWD/build-beaglev-ahead/qemu-system-riscv64" \
  build-beaglev-ahead/tests/qtest/beaglev-ahead-test \
  -p /riscv64/beaglev-ahead/usb/registers
```

Upstream isolation task:

* instantiate `usb-dwc3` directly in a small generic qtest, or use an existing
  upstream machine that realizes it, without any TH1520 source;
* show that DCTL access fails on the baseline and that `CSFTRST` self-clears
  after the minimal patch;
* check old-to-new migration in both directions permitted by QEMU policy; and
* search GitLab and qemu-devel for DWC3 DCTL/device-register/host-init reports.

This report is independent of the missing TH1520 glue driver and must not claim
that the minimal DCTL block implements DWC3 device mode.

### UQ-010: xHCI event-ring setup depends on ERSTBA half-write order

Status: **REPORTABLE; high confidence; Linux and qtest regressions exist;
generic isolation, 64-bit-access audit and duplicate search still required**

Affected upstream code:

* `hw/usb/hcd-xhci.c`, `xhci_runtime_write()`; and
* `hw/usb/hcd-xhci.h`, `XHCIInterrupter` migration state.

ERSTBA is a 64-bit event-ring-segment-table base exposed as two 32-bit words.
The baseline stores either half but calls `xhci_er_reset()` only after a write
to the high half.  Linux programs this register high half first and low half
second.  QEMU therefore reloads the event ring with the old low word (zero),
marks it disabled, then never reloads it after the real low address arrives.
No command-completion or port-change event is delivered and the guest waits
indefinitely for xHCI enumeration.

Expected result:

* programming both ERSTBA halves must activate the same event-ring table in
  either normal 32-bit write order.

Baseline result:

* low-then-high works, while high-then-low leaves `er_start`/`er_size`
  disabled even though ERSTBA reads back correctly.

Branch fix and evidence:

* commit `e51e183b1c` (`hw/usb: accept either xHCI ERSTBA write order`) records
  which half has arrived, reloads the event ring only after both halves have
  been written, resets that latch and migrates a partially programmed register
  with a backward-compatible VMState version;
* commit `af9076a779` adds `/riscv64/beaglev-ahead/usb/host-dma-irq`, which
  intentionally uses Linux's
  high-then-low order and proves an xHCI no-op command produces an event DMA
  and PLIC source 68; the full-build HID hotplug test retains low-then-high
  coverage and proves a port-change event; and
* after the fix, the pinned upstream Linux kernel enumerates QEMU keyboard
  `0627:0001` through the DWC3/xHCI host.

Current branch reproducer:

```sh
QTEST_QEMU_BINARY="$PWD/build-beaglev-ahead/qemu-system-riscv64" \
  build-beaglev-ahead/tests/qtest/beaglev-ahead-test \
  -p /riscv64/beaglev-ahead/usb/host-dma-irq
```

Upstream isolation task:

* move the order test to a generic qtest using `qemu-xhci` or another existing
  xHCI fixture and require identical command-event results for both orders;
* add a migration case after exactly one half has been written;
* audit true 64-bit MMIO reads/writes: the runtime region currently advertises
  accesses up to `sizeof(dma_addr_t)`, while the handler switches on one
  32-bit offset and returns a 32-bit value; and
* search GitLab and qemu-devel for ERSTBA, event-ring initialization and Linux
  xHCI timeout reports before filing.

This defect is in generic xHCI code and reproduces independently of TH1520.
It is not a claim about the board's provisional DWC3 synthesis values.

### UQ-011: RISC-V Sscofpmf interrupt CSRs have contradictory WARL masks

Status: **REPORTABLE; direct generic TCG reproducers; current-master and
duplicate recheck required**

Affected upstream code:

* `target/riscv/tcg/csr.c`, especially `delegable_ints`, `all_ints`,
  `rmw_mie64()`, `rmw_sie64()`, `rmw_mvip64()` and the `sip` masks.

The ratified Sscofpmf contract assigns local counter overflow to bit 13 in
`mip`/`mie`/`sip`/`sie`.  When the extension is present, `mie.LCOFIE` must be
writable and `mideleg` selects M- versus S-mode delivery.  When the extension
is absent, `mip.LCOFIP` and `mie.LCOFIE` must read as zero.

The baseline instead has two contradictory static masks:

* `all_ints` excludes fixed bit 13, so `rmw_mie64()` drops every attempt to
  enable LCOFI even when Sscofpmf is enabled.  The supervisor alias masks also
  use `S_MODE_INTERRUPTS | LOCAL_INTERRUPTS`, where `LOCAL_INTERRUPTS` starts
  at bit 16, so the S-mode views omit bit 13 as well; and
* `delegable_ints` includes `MIP_LCOFIP` unconditionally.  Consequently an
  `rv64,sscofpmf=false` guest can set bit 13 in `mideleg` and `mip`, even though
  the extension requires those fields to be read-only zero.

Direct baseline evidence:

* an extension-enabled payload writes `1 << 13` to `mie`, reads it back, and
  exits 5 when the bit is absent.  It exits 5 in both normal host-tick mode and
  with `-icount shift=0`; and
* an extension-disabled payload writes `1 << 13` to `mideleg` and `mip`, reads
  each back, and exits 1 when the unexpected `mideleg` bit is present.  It exits
  1 on the merged staging baseline.

The exact QEMU invocations are:

```sh
qemu-system-riscv64 -M virt -cpu rv64,sscofpmf=true \
  -display none -semihosting -bios test-sscofpmf-lcofie
qemu-system-riscv64 -M virt -cpu rv64,sscofpmf=false \
  -display none -semihosting -bios test-sscofpmf-warl-off
```

The enabled test must also be extended past CSR readback to program a near-wrap
counter, require an M-mode overflow interrupt, delegate it, and repeat through
`sie`/`sip` in S-mode.  The disabled test must require bit 13 to remain zero in
all four interrupt CSRs and in `mideleg`.

Likely fix direction:

* derive the LCOFI implemented mask from `ext_sscofpmf` rather than placing bit
  13 unconditionally in one static mask and omitting it from the others;
* use the same extension-aware mask consistently for `mie`, `mip`, `mideleg`,
  `sie` and `sip`, including AIA alias paths; and
* add extension-on/off CSR WARL, M-mode delivery, delegated S-mode delivery,
  acknowledgement and migration tests.

The specification evidence is the ratified RISC-V Privileged Architecture,
[Sscofpmf section 13.1.1](https://docs.riscv.org/reference/isa/v20260120/priv/sscofpmf.html)
and the
[machine interrupt-register description](https://docs.riscv.org/reference/isa/priv/machine.html).
A 2026-08-24 search for `Sscofpmf`, `LCOFIE`, `LCOFIP`, bit 13 and counter
overflow found no matching public QEMU GitLab issue or qemu-devel result.

### UQ-012: RISC-V PMU state does not continue correctly after migration

Status: **REPORTABLE; generic current-master fail-before and branch pass-after;
final human duplicate review required**

Affected upstream code:

* `target/riscv/machine.c`, the CPU VMState and post-load path; and
* `target/riscv/tcg/pmu.c`, the derived counter, event-map and overflow-timer
  state.

The migration stream carries `mhpmevent_val`, `mhpmcounter_val` and
`mhpmcounter_prev`, but this is insufficient to resume a running PMU:

* the destination's `pmu_event_ctr_map` remains empty, so a migrated
  programmable counter no longer receives its configured event;
* the host/icount snapshots in `pmu_fixed_ctrs` are process-local derived
  state, so fixed and instruction/cycle-backed programmable counters lose the
  source-side delta unless it is first materialized into their architectural
  values;
* the shared PMU overflow timer and per-counter long-deadline remainder are
  not migrated or reconstructed, so an armed near-wrap counter does not set
  its overflow state on the destination; and
* the architectural Smcntrpmf `mcyclecfg` and `minstretcfg` CSRs are absent
  from VMState altogether; and
* both migration callbacks are gated by the programmable-counter `pmu_mask`,
  and the post-load path returns when the event map is absent, even though
  fixed `mcycle`/`minstret` counters and their filters remain active when the
  mask is zero.

The generic qtest uses `virt` with
`rv64,pmu-mask=0x8,sscofpmf=true,smcntrpmf=true` and deterministic
`-icount shift=0`.  Its freestanding M-mode guest selects standard instruction
event 2 in counter 3, writes nonzero fixed-counter filter CSRs, accumulates and
records both programmable and fixed-counter source values, arms counter 3 near
wrap, and stops for file migration.  Without reprogramming any PMU CSR on the
destination, it requires:

* both Smcntrpmf configuration values to survive;
* fixed `mcycle` and programmable counter 3 to start at or beyond their
  recorded source values and continue advancing;
* counter 3 to cross its wrap point; and
* `mhpmevent3.OF` to become set by the reconstructed timer.

An independent fixed-only guest uses
`rv64,pmu-mask=0,smcntrpmf=true`, excludes a long U-mode interval with
`mcyclecfg`/`minstretcfg`, and requires the destination delta to remain within
a tight bound.  Parent commit `5222c7bc47` fails it with guest status
`0xdead1003`; commit `2d237bdfde` makes the migration hooks unconditional for
TCG CPUs and always rebases fixed counters, so the test passes.

Equivalent BeagleV Ahead variants select C910 event 22 and check the vendor
`MCOUNTEROF` bit instead of standard `mhpmevent3.OF`.  Separate generic and
C910 guests also migrate an inhibited counter at an exact frozen value, verify
its selector and inhibit state, and restart it on the destination.  Already-
pending overflow guests preserve the standard OF or vendor `MCOUNTEROF` bit
and `mip.LCOFIP`, clear both, reprogram the counter and observe a second
overflow.  All branch tests pass.  Exact QEMU `master` commit
`bde2492aace2b5acb755a5b057013e915163a77f` fails the strengthened generic
test first with guest status `0xdead0005` because the Smcntrpmf configuration
is lost.  Before that check was added, the same revision failed with
`0xdead0001` because the destination counter did not advance.  Fault-isolation
runs on the strengthened test independently produce
`0xdead0005` when configuration serialization is disabled, `0xdead0004` when
source materialization is disabled, `0xdead0001` when event-map reconstruction
is disabled, and `0xdead0002` when timer rearming is disabled.  The complete
fix returns status 3.

Branch commits `aff489d2f7` (`target/riscv: Reconstruct PMU state after
migration`) and `2d237bdfde` (`target/riscv: Migrate fixed counters without
HPM counters`) together:

* materialize instruction/cycle-derived values in a CPU `pre_save` callback
  while rebasing the source snapshots so a cancelled migration can continue;
* serialize nonzero `mcyclecfg` and `minstretcfg` in an optional VMState
  subsection;
* clear and rebuild the destination event map from the migrated selectors;
* rebase process-local fixed-counter snapshots to the destination clock;
* recompute all eligible non-pending overflow deadlines from the migrated
  architectural counter values; and
* run the fixed-counter materialization and rebase paths even when no
  programmable HPM counters are implemented.

The complete normal RISC-V qtest suite passes 17 test binaries with one skip,
including 98 BeagleV Ahead and ten CSR/PMU subtests.  The dependency-minimal
and ASan/UBSan suites each pass ten binaries with three expected skips,
including 97 board and four CSR/PMU subtests.  The complete normal RISC-V TCG
guest suite also passes.

Run the focused generic regression with:

```sh
QTEST_QEMU_BINARY=build/qemu-system-riscv64 \
  build/tests/qtest/riscv-csr-test -p /riscv64/cpu/pmu-migration

QTEST_QEMU_BINARY=build/qemu-system-riscv64 \
  build/tests/qtest/riscv-csr-test -p /riscv64/cpu/fixed-pmu-migration
```

A 2026-08-24 quick search of QEMU GitLab and qemu-devel for RISC-V PMU
migration, `mhpmevent`, `pmu_event_ctr_map`,
`riscv_pmu_update_event_map` and post-load timer rearming found no matching
report or patch.  This was not exhaustive.  Before filing, repeat the search
manually, rerun the test against then-current `master`, decide whether
maintainers prefer the missing Smcntrpmf fields split from derived-state
reconstruction, and disclose the agent-assisted discovery.

## Already reported or addressed upstream

### UQ-K001: NPCM GMAC transmit buffer integer truncation

Status: **PUBLIC DUPLICATE; do not create a new issue**

Upstream `gmac_try_send_next_packet()` uses a 16-bit allocation-size variable.
A sufficiently long guest-controlled descriptor chain wraps that size before
the DMA copy.  This is already public as
[QEMU issue 3202](https://gitlab.com/qemu-project/qemu/-/work_items/3202).

Commit `95af4a301b` independently changes the allocation size to `size_t`,
checks accumulated frame length before addition/copy, and bounds descriptor
walks.  Before proposing any subset, compare it with the issue's current patch
and coordinate rather than sending a competing duplicate fix.

### UQ-K002: RISC-V TCG user-option tables leak during multi-hart initialization

Status: **PUBLIC PATCH DUPLICATE; do not create a new issue or competing patch**

Exact QEMU `master` commit
`bde2492aace2b5acb755a5b057013e915163a77f` assigns newly allocated
`multi_ext_user_opts` and `misa_ext_user_opts` tables every time
`riscv_tcg_cpu_instance_init()` runs.  Those pointers are process-global, so
each hart except the last loses both previous tables.  LeakSanitizer reports
960 bytes in 18 allocations after a four-hart BeagleV Ahead qtest process.

Local commit `79dd49c99b` clears and reuses the existing tables and makes the
sanitizer gate clean.  The public June 2026 v3
[target/riscv no-TCG build series](https://patchew.org/QEMU/20260602091753.3209261-1-fritchleybohrer%40gmail.com/20260602091753.3209261-2-fritchleybohrer%40gmail.com/)
already uses a broader clear-and-reallocate fix.  Rebase onto that series when
it lands, or coordinate with its author if a smaller backport is needed; do not
file a duplicate report from this project.

## Investigation candidates not included in the twelve-report tally

### UQ-C001: `pmpaddr` retains bits above the implemented address width

Status: **NEEDS SPEC CHECK AND ISOLATED REPRODUCER**

At the baseline, `target/riscv/tcg/pmp.c:pmpaddr_csr_write()` stores the guest
value without masking bits above `pmp_addr_bits`.  Commit `ffd9d62710` masks
them, and commit `cce292534e` separates the physical-address width concept from
whether PMP is present.  This looks like incorrect WARL behavior for CPUs with
a narrower implemented physical address, but the reviewer must check the
current privileged specification, interaction with RV32's extra PMP address
bits, and existing CPU properties before reporting it.

Required test: configure a CPU with a deliberately narrow width, write all
ones to an unlocked `pmpaddr`, read it back, and exercise TOR/NAPOT boundary
translation.  Search tracker and mailing-list history for prior PMP-width
patches.

### UQ-C002: cyclic NPCM GMAC descriptor chains can monopolize QEMU

Status: **POSSIBLE SECURITY/DOS ISSUE; NEEDS CONTROLLED REPRODUCER**

The baseline transmit path has an unbounded `while (true)` descriptor walk,
and the receive path can also make no progress through guest-created cyclic or
zero-length chains.  Commit `95af4a301b` caps each walk at 65536 descriptors
and reports an unavailable/error condition.  A malicious guest may be able to
keep the QEMU thread in the device callback indefinitely.

Reproduce only under an external timeout, determine whether existing ownership
or ring semantics create a legitimate bound, and initially use a confidential
GitLab item if the hang is confirmed.  Keep this distinct from public transmit
overflow issue 3202 unless maintainers request consolidation.

### UQ-C003: RISC-V PMU overflow deadlines assume host ticks are nanoseconds

Status: **SOURCE-LEVEL TIMING MISMATCH; NEEDS A DETERMINISTIC CONTRACT TEST**

Without instruction counting, RISC-V cycle and cycle-selected HPM counters use
`cpu_get_host_ticks()`.  On x86-64 this is `rdtsc`; other hosts use different
architecture-specific counters.  `pmu_icount_ticks_to_ns()` nevertheless
converts an overflow delta using the fixed `RISCV_TIMEBASE_FREQ` value of 1 GHz
and schedules it on `QEMU_CLOCK_VIRTUAL`.  QEMU has no proof that the host tick
source advances at 1 GHz, so the counter value and its scheduled wrap deadline
are expressed in different units.

The C910 near-wrap payload was intermittent without `-icount` and passed 20 of
20 repetitions after its runner selected `-icount shift=0`.  That is enough to
make the branch regression deterministic, but not enough by itself to prove an
architectural failure: interrupt-pending propagation may be delayed, and a
busy-loop timeout is host-speed-sensitive.  Keep this separate from UQ-003's
integer-width defects.

Before promotion, construct a generic unit or TCG test that compares the
counter's actual wrap with the virtual timer deadline on hosts whose tick
frequency is demonstrably not 1 GHz.  Establish the intended non-icount cycle
frequency with the RISC-V maintainers, then either schedule in the same clock
domain or use a calibrated conversion.  Recheck existing PMU timer discussions
before filing.

## Findings deliberately excluded from upstream bug reports

### UQ-L001: BeagleV Ahead accepted `-dtb` but ignored it

Status: **FIXED LOCAL IMPLEMENTATION DEFECT; NOT AN EXISTING UPSTREAM BUG**

The new `beaglev-ahead` machine originally always generated its own FDT even
when `MachineState::dtb` was set.  Controlled Linux timer validation exposed
the problem.  The current worktree uses `load_device_tree()` when `-dtb` is
provided, and `/beaglev-ahead/boot/external-dtb` proves that the marker tree is
actually handed to the guest.

Because the BeagleV Ahead machine is not yet in upstream QEMU, this belongs in
the board patch series and must not be presented as a defect in a released
upstream machine.

### UQ-L002: the local C910 model advertised Zfh but rejected Zfhmin operations

Status: **FIXED LOCAL CPU-MODEL DEFECT; NOT AN EXISTING UPSTREAM BUG**

The new C910 definition enabled `Zfh` on its Privileged-1.10 baseline, but the
required `Zfhmin` execution flag remained false and the generic privileged
version filter did not permit that implied dependency.  As a result, valid
`flh` and `fsh` instructions trapped illegally even though the model and its
generated ISA string claimed Zfh.

The current worktree enables Zfhmin explicitly for C910, treats both Zfh and
Zfhmin as legacy C910 extensions independent of QEMU's newer generic
registration floor, and executes half-precision misaligned loads/stores in the
MXSTATUS.MM payload.  Upstream's existing CPU models do not reproduce this
C910-only configuration, and the baseline has no C910 definition, so this is a
patch-series review finding rather than an upstream report unit.  A reviewer
may still audit generic implied-extension/version filtering separately, but it
must have an upstream reproducer before promotion.

### UQ-L003: the local XTheadVector port allowed misaligned loads and stores

Status: **FIXED LOCAL CPU-MODEL DEFECT; NOT AN EXISTING UPSTREAM BUG**

The imported XTheadVector element helpers used unaligned QEMU data-access
functions directly.  Misaligned vector loads and stores therefore completed,
contrary to the pinned openC910 LSU rule that forces vector memory accesses to
trap independently of MXSTATUS.MM.  The current worktree applies `MO_ALIGN`
at the memory element width to the shared unit-stride, strided, indexed,
segmented and fault-only-first callbacks.  The C910 payload requires
misaligned word-vector load/store traps while scalar MM remains enabled.

Neither XTheadVector nor these imported helpers exist in the upstream baseline.
This must be fixed and reviewed in the eventual XTheadVector series, not filed
as a defect in released upstream QEMU.  XTheadZvamo remains disabled and is a
separate hardware-availability question under ledger item `CPU-013`.

### UQ-L004: standard RVV translation stole overlapping XTheadVector stores

Status: **FIXED LOCAL CPU-MODEL INTEGRATION DEFECT; NOT AN EXISTING UPSTREAM BUG**

The generic RISC-V decoder runs before the branch's XTheadVector decoder.
Several legacy encodings overlap ratified RVV, including the C910 word-vector
store used by the MAEE payload.  `require_rvv()` checked only the dynamic VS
state.  Because XTheadVector makes vector state valid without enabling Zve32x,
the standard `vse32.v` translation accepted the word first and bypassed the
legacy helper.  A strong-order vector load consequently faulted as intended,
while the adjacent store completed and the payload exited at stage 13.

The worktree now requires Zve32x before any ratified-RVV translation.  Full V
implies Zve32x through QEMU's existing extension rules, so standard V/Zve CPUs
retain their decoder.  The strong-order store now reaches XTheadVector and
raises the RTL-derived zero-`tval` store/AMO access fault.  The same payload
also proves that an allowed non-cacheable store still completes, and the
complete normal RISC-V TCG suite keeps the standard Zicclsm/vector cases green.

This collision is not independently reachable on the audited upstream
baseline: without a standard vector extension, upstream sets VILL in the TB
state, and upstream does not yet contain this XTheadVector implementation.
Treat the gate as a required part of the future XTheadVector patch series and
do not file it as a released-upstream defect unless a baseline-only reproducer
is found.

### UQ-L005: the local C910 model faulted on MAEE PTE bits while MAEE was clear

Status: **FIXED LOCAL CPU-MODEL DEFECT; NOT AN EXISTING UPSTREAM BUG**

The branch initially made PTE[63:59] legal only while MXSTATUS.MAEE was set.
Clearing MAEE therefore reinterpreted the same C910 page-table entry as a
standard Sv39 entry and raised a page fault for its high attribute bits.  The
test encoded that provisional assumption, so the implementation and its
regression agreed with each other but not with the core RTL.

At pinned openC910 commit
`b91c90914c19f114d35c8f6b73408eb241ed847c`, the page-fault expression in
`C910_RTL_FACTORY/gen_rtl/mmu/rtl/ct_mmu_ptw.v` does not test PTE[63:59].
The `ptw_ref_pma` mux takes those bits when `cp0_mmu_maee` is asserted and
takes `sysmap_mmu_flg3` when it is clear.  The MMU-off data and instruction
TLB paths independently source the same physical system-map flags.  Thus the
five bits remain owned by XTheadMaee in either state; disabling MAEE changes
the selected attributes rather than PTE validity.

The worktree now excludes those bits from reserved/PBMT/NAPOT processing for
an XTheadMaee-capable first-stage walk regardless of the current MAEE value,
but applies their attributes only while MAEE is enabled.  The freestanding
payload clears MAEE, accesses four aliases with distinct PTE attributes and
requires identical successful data reads with no trap.  Normal, minimal and
sanitizer executions pass.

QEMU still does not know the TH1520 synthesis-specific physical PMA ranges.
The generic ranges in openC910's generated `sysmap.h` do not match the TH1520
memory map and must not be copied into the board model.  The branch now has an
immutable eight-region integration path and an explicitly synthetic 38-trap
M/S/U regression for direct, Bare and Sv39 selection.  The BeagleV machine
leaves that table invalid until authoritative integration data or physical
probes establish its values, so MAEE-disabled and Bare accesses retain QEMU's
ordinary attributes by design.  This explicit limitation is tracked under
`CPU-004`; plumbing coverage is not evidence of complete silicon emulation.

The upstream baseline has no C910 CPU or XTheadMaee implementation, so it
cannot reproduce this defect.  Keep the fix and corrected test in the future
C910 series.  Do not file it as an upstream QEMU bug unless an independent
problem is found in a pre-existing CPU's standard Sv39 handling.

The following are also not counted as upstream QEMU bugs at present:

* devices and SoC blocks that upstream simply does not emulate yet;
* uncertain C910/TH1520 hardware behavior awaiting owner-board validation;
* Linux's early DW APB timer clockevent/PLIC ordering and unavailable early
  TH1520 clock provider, which are Linux/device-tree integration questions;
* MR75203 alarm/timer/IRQ/timing fidelity and local implementation mistakes
  found and fixed before submission, because the device model is not upstream;
* optional C910 PMP presence, reset topology, or `THEADISAEE` semantics until
  public evidence and physical tests establish the exact contract; and
* fidelity limitations explicitly recorded as open hardware-validation items.

## Suggested review order

1. `UQ-006` first, privately, because it may disclose host memory.
2. `UQ-K001` and `UQ-C002`, to avoid duplicating or conflicting with the
   existing NPCM security work.
3. Small isolated fixes: `UQ-009`, `UQ-010`, `UQ-008`, `UQ-005`, and
   `UQ-001`.
4. Migration fixes: `UQ-004` and `UQ-007`, plus the compatibility portions of
   `UQ-009` and `UQ-010`.
5. Generic RISC-V PMU work: `UQ-011`, `UQ-012`, `UQ-002` and `UQ-003`.
6. Only then decide whether `UQ-C001` or `UQ-C003` has enough specification
   and test evidence to promote into the reportable list.

For each completed review, record the current upstream commit, failing and
passing commands, sanitizer output where relevant, duplicate-search terms,
issue URL, patch Message-ID, and final disposition in this file.
