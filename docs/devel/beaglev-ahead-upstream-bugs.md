# Upstream QEMU bug handoff from BeagleV Ahead work

This document records defects in pre-existing upstream QEMU code that were
found while implementing BeagleV Ahead support.  It is a reviewer handoff,
not a claim that an issue has been accepted by QEMU or assigned a CVE.

The audit baseline is QEMU `staging` commit
`2be159078ea26feac4c9c9902acf8906f1a05c2a` fetched on 2026-08-24 and merged
into this workspace by `2d7bb62c70`.  Recheck every item against current
`master` immediately before reporting it.

## Snapshot and counting rules

The current conservative tally is:

* **10 proposed new upstream report units** (`UQ-001` through `UQ-010`);
* **1 matching public upstream report** (`UQ-K001`), which is not new;
* **2 additional investigation candidates** (`UQ-C001` and `UQ-C002`);
* **3 defects confined to this not-yet-upstream board/CPU implementation**
  (`UQ-L001` through `UQ-L003`), which must not be reported as existing
  upstream bugs; and
* **0 reports filed by this project so far**.

A source-path audit of the 97 upstream commits between the prior
`bde2492aace2b5acb755a5b057013e915163a77f` snapshot and the baseline above
found changes only in `target/riscv/cpu.c` and `target/riscv/tcg/pmu.c` among
the files implicated by UQ-001 through UQ-010.  The incoming Zicclsm CPU-option
work and `minstret` exception-accounting fixes do not address the zero-ID,
duplicate-event-map or overflow-width findings.  None of the SDHCI, AT24C,
NPCM GMAC, DWC3 or xHCI candidate paths changed in that range.  The
conservative report tally therefore remains 10 after the staging merge; this
path audit is not a substitute for the required current-master reproducer and
duplicate search.

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
integration gaps, not QEMU bugs.  The conservative QEMU tally remains 10.

The 2026-08-24 board-LED milestone did not add a QEMU report unit either.
Upstream's generic LED device already accepts GPIO input and migrates its
intensity; exposing that intensity read-only through QOM and wiring board
consumers are feature work.  An older vendor Linux DTS calls the five user
LEDs green, while the schematic, BOM and pinned mainline DTS identify them as
blue.  That stale cross-project label is recorded under ``BOARD-002`` and is
not a defect in pre-existing QEMU.  The conservative QEMU tally remains 10.

The 2026-08-24 reset-coupling milestone also did not add a QEMU report unit.
It connects Linux-described TH1520 AP and storage reset groups to devices
introduced or integrated on this branch.  The collapsed whole-device reset
scope and remaining hardware unknowns are new-board fidelity work under
``RST-001``, not defects in QEMU's pre-existing machines or reusable devices.
The conservative QEMU tally remains 10.

The 2026-08-24 clock-gate milestone did not add a QEMU report unit.  Exporting
TH1520 leaf-gate state, wiring new SoC clock links and defining provisional
pause/resume behavior for the branch's PWM, timer and watchdog integrations
are new-board fidelity work under ``CLK-002``.  Focused normal and migration
tests found no independent defect in a pre-existing upstream machine.  The
conservative QEMU tally remains 10.

The 2026-08-24 C910 MXSTATUS.MM milestone did not add a report unit.  It found
two real implementation defects: the branch's new C910 definition exposed
Zfh while losing its Zfhmin dependency, and its imported XTheadVector helpers
accepted misaligned loads/stores.  Because neither the C910 model nor that
XTheadVector implementation exists at the upstream baseline, both are local
patch-series defects (`UQ-L002` and `UQ-L003`) rather than bugs in released
upstream QEMU.  The generic scalar Zicclsm implementation remains independent
and passes its enabled/disabled tests.

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

Status: **REPORTABLE; high source-level confidence; isolate a generic test**

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
  being reprogrammed; and
* the modified code is exercised by the C910 PMU test, but that test does not
  yet isolate this generic duplicate-selection case.

Upstream isolation task:

* use a generic RISC-V CPU with at least two HPM counters;
* select the same instruction event in counters 3 and 4, execute a bounded
  instruction sequence, and require both counters to advance; and
* reprogram only one counter and prove the other mapping remains active.

No matching public issue was found in the 2026-08-23 quick search.

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

## Already reported upstream

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

## Investigation candidates not included in the ten-report tally

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
5. Generic RISC-V PMU work: `UQ-002` and `UQ-003`.
6. Only then decide whether `UQ-C001` has enough specification and test
   evidence to promote into the reportable list.

For each completed review, record the current upstream commit, failing and
passing commands, sanitizer output where relevant, duplicate-search terms,
issue URL, patch Message-ID, and final disposition in this file.
