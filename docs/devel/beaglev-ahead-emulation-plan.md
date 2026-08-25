# BeagleV Ahead / TH1520 QEMU emulation plan

Status: portable Linux eMMC-root/process-reopen and PLL-polling checkpoints
validated; official-image, portable-Linux-6.11.9 SMP reproduction and
physical-card validation remain open, 2026-08-25

Board: BeagleV Ahead, Seeed/BeagleBoard SKU 102991698

QEMU baseline: 2be159078ea26feac4c9c9902acf8906f1a05c2a

Workspace integration commit: 2d7bb62c70 (merge of the baseline above)

GMAC receive-filter checkpoint: 5dd4f5794b

GMAC Type-2 receive-status checkpoint: 46df230d5d

GMAC transmit-checksum checkpoint: f441cf709f

User mask-ROM checkpoint: 0d45dbf7b6

Hardware evidence baseline: beagleboard/beaglev-ahead
6b56e2d69485c375c5912eaa2791f79f1d089c07

Linux evidence baseline: 2709dd5ae32f0828f386327c76bba9f39f63a1c6

The companion
[hardware validation ledger](beaglev-ahead-hardware-validation.md) records
every fact that is not yet proved.  An implementation must not silently turn
one of those unknowns into a guessed hardware behavior.

The companion
[physical PMA design](beaglev-ahead-physical-pma.md) defines the eight-region
C910 integration contract and the safe evidence/probe gate for the still
unknown TH1520 system map.

The companion
[upstream bug handoff](beaglev-ahead-upstream-bugs.md) separates defects found
in pre-existing QEMU code from missing emulation, hardware unknowns, and bugs
confined to this not-yet-upstream board implementation.  It records the
reproducer and disclosure work required before any external report.

The companion
[local implementation audit](beaglev-ahead-local-bugs.md) is the focused
checklist for branch-only defects, current normal/sanitizer evidence, and
fidelity gaps that must not be mislabeled as upstream bugs.

## Current milestone and handoff

Per owner direction, work is focused on the local BeagleV Ahead QEMU rather
than upstream bug reporting.  The current bounded milestone adds an opt-in
eMMC 5.1 speed profile only to the BeagleV Ahead eMMC attachment; generic QEMU
eMMC users retain their existing defaults.  The synthetic profile reports
``EXT_CSD_REV = 8``, ``CARD_TYPE = 0x57`` and
``GENERIC_CMD6_TIME = 50`` (a conservative 500 ms fallback), with zero
``STROBE_SUPPORT`` and zero/default ``DRIVER_STRENGTH`` (Type 0).  It validates
CMD6 bus-width and timing changes, emits the standard 64-byte four-bit and
128-byte eight-bit CMD21 tuning blocks, and makes SDHCI Execute Tuning consume
the card data internally.  The reference qtest workflow exercises HS200 and
CMD21 before moving through HS and DDR8 to HS400.  The card does not retain a
"tuned" history bit: it requires the immediate HS-plus-DDR8 predecessor state
for HS400, while unsupported direct transitions report ``SWITCH_ERROR``.

Three focused qtests cover this change:
``/beaglev-ahead/dwcmshc/sd-cmd19-tuning``,
``/beaglev-ahead/dwcmshc/emmc-hs400-profile`` and
``/beaglev-ahead/dwcmshc/emmc-tuning-migration``.  The preserved pre-change
binary fails deterministically at ``EXT_CSD_REV`` 5 rather than 8.  The full
board suite passes 113/113 in the normal build and 112/112 in the
dependency-minimal build; the complete 13-test storage group passes under
ASan/UBSan.  Generic ARM ``xlnx-zcu102`` and PCI SDHCI libqos register paths
also pass.  The standalone NPCM SDHCI suite has not been rerun.

Unchanged pinned Linux boots in both normal and dependency-minimal QEMU and
reports ``mmc1: new HS400 MMC card``.  A QEMU command trace proves its CMD6
HS200-to-HS-to-DDR8-to-HS400 transitions but no CMD21.  This is intentional in
the pinned TH1520 Linux driver: its platform tuning callback returns success
without sending CMD21 while ``SDHCI_HS400_TUNING`` is set.  Linux therefore
validates EXT_CSD negotiation, CMD6 and HS400 enumeration, while the focused
qtests remain the CMD21 data/IRQ gate.  A complementary vendor-U-Boot run and
an end-to-end guest that actually issues CMD21 remain pending.

A new portable functional gate pins Linux 6.11.9 and a small RISC-V ext2
rootfs by SHA-256.  With one hart, Linux enumerates HS400, mounts the eMMC
image as root, enters a controlled root shell, writes/syncs and hashes a
deterministic 1 MiB file, remounts read-only, then closes QEMU.  A fresh QEMU
process reopens the same scratch image and verifies the hash.  Normal and
dependency-minimal builds pass.  This is deliberately a storage-specific
root-shell gate: it does not prove normal distro init, SMP, host-cache
eviction, power-loss durability, ``e2fsck``, stress, or an official board
image.  ``/dev/mmcblk1`` is stable for this pinned setup but remains dependent
on Linux probe order.

The same checkpoint fixes a TH1520 AP-clock scheduling defect found by Linux's
PLL poll.  A guest could advance virtual time beyond the modeled lock deadline
before the I/O thread dispatched the timer callback, leaving ``PLL_STS`` stale
past Linux's timeout.  Status reads now materialize an already-expired deadline
through the existing lock helper.  A raw RV64 single-threaded-TCG qtest fails
with the preserved pre-fix binary and passes twice across system reset with the
fix; focused ASan/UBSan CPR tests pass.  The modeled delay, migration and reset
contract are unchanged, and this is not evidence for silicon PLL timing.

One non-blocking migration coverage gap is explicit: the current test migrates
an armed Execute Tuning request and then observes ``RBUFRDY`` on the
destination, but it clears that interrupt before the next migration.  SDHCI
VMState already carries the interrupt status/enable/signal words and post-load
recomputes the IRQ; a later test should nevertheless migrate an already-pending
``RBUFRDY`` directly.

This is deliberately a software compatibility profile, not a claim about the
device fitted to the owner's board.  Its identity, CID, CSD, complete EXT_CSD,
voltage behavior and electrical/analog HS200/HS400 timing remain open under
``SD-001`` and ``SD-002``.

The official-image gate remains blocked under ``DOC-002``.  The pinned
BeagleBoard flashing documentation names ``boot.ext4``, ``root.ext4``,
``u-boot-with-spl.bin`` and ``fastboot_emmc.sh`` in a target named
``xuantie-ubuntu-<job-ID>.zip``, but the linked artifact could
not be pinned through the available GitLab API/jobs page.  The portable assets
above do not substitute for that official-image evidence.

Upstream triage remains deferred.  The two companion bug documents continue
to separate pre-existing QEMU issues from branch-only implementation gaps, and
no external report has been filed from this workspace.  After this checkpoint,
select the next local fidelity gate or begin the documented read-only hardware
capture.  Hardware bring-up, including the UART adapter connection and every
item marked OPEN in the validation ledger, remains the authoritative evidence
source for physical fidelity.

## Executive answer

At the QEMU baseline above, usable BeagleV Ahead emulation is absent:

* There is no beaglev-ahead machine and no TH1520 SoC model.
* There is no thead-c910 CPU type.
* QEMU can execute the ordinary RV64 integer, atomic, floating-point, and
  compressed instructions used by the C910, and implements ten T-Head scalar
  extension groups.
* QEMU does not implement XTheadVector, the C910's incompatible RISC-V Vector
  0.7.1 instruction set.
* QEMU's current T-Head CSR support is a small C908-oriented compatibility
  layer.  Most declared custom CSRs read zero; MAEE is explicitly unimplemented.
* Several generic IP models are useful foundations, but no existing QEMU
  machine connects them at TH1520 addresses or with TH1520 reset, interrupt,
  DMA, and clock behavior.

Consequently, a stock BeagleV Ahead firmware or OS image cannot be expected to
boot on current upstream QEMU.  There is no honest single completion
percentage: board compatibility is zero, while some CPU instructions and
several reusable IP blocks already exist.

## Meaning of “perfect”

The target is software-visible functional fidelity:

1. The same unmodified firmware, boot media, kernels, device trees, drivers,
   and relevant applications run on the emulator and the physical board.
2. Architecturally visible CPU, CSR, exception, MMU, interrupt, register,
   DMA, reset, clock-gating, and power-domain behavior matches hardware.
3. Device state is deterministic, resettable, testable, and migratable where
   QEMU supports migration for the corresponding class of device.
4. Invalid and boundary behavior is covered, not only the happy boot path.
5. Differences that require analog, RF, physical-link, or silicon-timing
   behavior are explicitly documented and represented through QEMU backends or
   fault-injection controls when useful.

Cycle-accurate timing, exact performance, analog signal integrity, radio-wave
simulation, power consumption, and undocumented GPU/NPU microarchitecture are
not realistic QEMU goals.  For those areas, “perfect” means the strongest
software-visible contract that can be documented and verified.  If proprietary
firmware or unavailable specifications prevent that contract, the limitation
remains an open release blocker in the ledger.

## Current progress estimate

As of 2026-08-25, this branch is approximately **53% complete (plus or minus
5 percentage points)** against the strict definition above and approximately
**87% complete for practical C910 Linux and driver development**.  These are
weighted engineering estimates, not a ratio of files or register blocks.  The
strict number remains dominated by authentic boot/reset, exhaustive CPU
differential validation, USB device/OTG and PHY behavior, display/GPU,
media/NPU, auxiliary processors, security/power domains, stock-image coverage,
physical comparison and final source pruning.

The USB-host submilestone is about **90% complete**: the TH1520 wrapper,
miscellaneous resets, DWC3/xHCI host, DMA, PLIC IRQ, one USB2/USB3 connector,
hotplug, migration and upstream-Linux keyboard enumeration work.  Its remaining
host gaps are exact silicon capabilities, PHY/link/timing, clock gating,
suspend/resume, stress/error injection and physical comparison.  This does not
mean Phase 7 is 90% complete: USB device/OTG/Fastboot plus Wi-Fi and Bluetooth
remain largely open.

The RTC submilestone is about **90% complete**: the reusable X-Gene-compatible
device, TH1520 address/clock/PLIC wiring, generated disabled node, 1 Hz
prescaler path, set/read/alarm behavior, reset and migration tests, and a
pinned-mainline Linux proof all pass.  Exact component identity, prescaler and
wrap edges, calibration, battery/reset retention, suspend wake and physical
comparison remain open.  Mainline Linux also needs a TH1520-specific binding
and prescaler contract before the generated node can safely be enabled.

The board-LED submilestone is about **85% complete**: five active-high blue
user LEDs are connected to GPIO4 pins 8-12, the green power LED is always on,
their intensity is observable through QOM, and reset, migration and pinned
Linux ``gpio-leds`` tests pass.  The schematic and BOM strongly establish the
wiring and colors, but first-power-up state, polarity, brightness and reset
behavior still require comparison with the owner's exact assembly.  Buttons
remain unwired.

The reset-coupling submilestone is about **75% complete**.  All reset members
published by mainline Linux for the currently modeled AP peripherals now
drive QEMU device resets: both watchdogs, PWM, both timer groups, UART0-5,
I2C0-5, SPI0, GPIO0-3, both application pad controllers, DMAC0, both GMACs and
their shared AXI domain.  The miscellaneous-system eMMC and two SDIO reset
members and all three USB members are also connected.  Qtests cover all 28 AP
outputs and three storage outputs, per-device reset effects, neighbor
isolation and asserted-line migration.  QEMU intentionally collapses each
multi-bit APB/core/AXI group to a whole-device cold reset and leaves MMIO
accessible while held; mailbox, C910-hart, AO and auxiliary-domain resets,
functional gate effects for untimed children, physical reset scope, retention
and ordering remain open under ledger item ``RST-001``.

The clock-gate submilestone is about **45% complete**.  All 33 mainline-defined
AP leaf gates whose consumers exist in this machine and all eight represented
miscellaneous-system gates now have active-high observable outputs rebuilt
from register state after reset and migration.  The PWM, both timer-group and
both watchdog leaves additionally drive QEMU Clock links: clearing a gate
freezes an active phase/count and re-enabling it resumes from that point.
Focused qtests cover every raw output, neighbor isolation, timed consumers,
system reset and migration while gated.  The other 28 AP leaves and all eight
miscellaneous leaves remain raw state only; parent-gate dependencies, bus
fault/access behavior, rate changes, physical phase/output behavior and all
AO/video/DSP/power-domain clocks remain open under ``CLK-002``.

The pinned mainline and vendor device trees plus the board schematic expose no
PCIe controller, endpoint or routed connector for BeagleV Ahead.  PCIe is
therefore not counted as missing board emulation; the physical PCB revision
will still be checked under ledger item ``BOARD-003``.

## Pinned source set and provenance

The implementation must cite and pin the evidence used for each block:

* QEMU upstream master at the baseline above.
* Linux TH1520 and BeagleV Ahead device trees:
  arch/riscv/boot/dts/thead/th1520.dtsi and
  arch/riscv/boot/dts/thead/th1520-beaglev-ahead.dts.
* Official BeagleBoard documentation:
  https://docs.beagleboard.org/boards/beaglev/ahead/
* Official BeagleBoard hardware repository at the baseline above:
  https://github.com/beagleboard/beaglev-ahead
* T-Head extension specifications:
  https://github.com/XUANTIE-RV/thead-extension-spec at commit
  e744688edd2f88be2e032c67e20789030436ac08
* Official openC910 RTL at commit
  b91c90914c19f114d35c8f6b73408eb241ed847c:
  https://github.com/XUANTIE-RV/openc910
* OpenSBI at commit 4e79fd7de59f1b2899092c1a84ce68c8ebc68f93,
  including its ``thead,c900-clint`` and ``thead,c900-plic`` integration
  quirks: https://github.com/riscv-software-src/opensbi
* Alibaba/XuanTie QEMU's XTheadVector implementation at commit
  3287d345c7f5d60d5c8774d90752f5f710744f85, reconciled with the older
  upstream RVV 0.7.1 implementation at
  e523773040ed914b60c8b68c25a96c88b2bb112a.  The retained GPL and copyright
  notices and upstream provenance review are tracked as DOC-003.
* Portable functional-test Linux 6.11.9 ``Image`` from
  https://storage.tuxboot.com/kernels/6.11.9/riscv64/Image, SHA-256
  ``174f8bb87f08961e54fa3fcd954a8e31f4645f6d6af4dd43983d5e9841490fb0``.
* Portable functional-test RISC-V ``rootfs.ext2.gz`` from
  https://github.com/groeck/linux-build-test/raw/9819da19e6eef291686fdd7b029ea00e764dc62f/rootfs/riscv64/rootfs.ext2.gz
  at linux-build-test commit ``9819da19e6eef291686fdd7b029ea00e764dc62f``,
  SHA-256
  ``b6ed95610310b7956f9bf20c4c9c0c05fea647900df441da9dfe767d24e8b28b``.
  These two assets are publicly downloadable, hash-pinned test inputs, not
  official BeagleV Ahead firmware or an official image.
* Firmware and kernel versions used by an official BeagleV Ahead image, pinned
  by commit and image hash before compatibility work starts; this remains open
  under ``DOC-002``.

The official hardware repository publicly distributes nine TH1520 manuals,
but the pages are marked “Secret” and carry restrictive copyright notices.
Facts may be summarized and independently implemented, but the PDFs and large
verbatim extracts must not be copied into this repository.  Before proposing
derived work upstream, document provenance and obtain a maintainer/legal
decision under DOC-001 in the ledger.

## Hardware scope

The TH1520 contains substantially more than its four application cores:

* four 64-bit C910 application cores, 64 KiB I-cache and D-cache per core,
  shared 1 MiB L2, Sv39, 40-bit physical address space, 240-source PLIC, and a
  3 MHz architectural timer;
* one 64-bit C906 audio/voice processor;
* one 32-bit E902 low-power controller;
* two Cadence/Tensilica Vision Q7 DSPs;
* BootROM, SRAM/retention RAM, DDR, interrupt, clock, reset, power, mailbox,
  DMA, security, and system-control fabrics;
* eMMC, two SDIO controllers, two QSPI controllers, two GMACs, USB 3,
  UART/I2C/SPI/GPIO/PWM/timer/RTC/watchdog/ADC and audio interfaces;
* display, HDMI/DSI, GPU, NPU, camera/ISP, video codec, and image-processing
  engines.

The BeagleV Ahead board adds 4 GiB LPDDR4, 16 GiB eMMC, microSD, RTL8211F
Gigabit Ethernet PHY, AP6203BM/CYW43012 Wi-Fi/Bluetooth, micro-HDMI, two CSI
connectors, DSI, USB, EEPROM, LEDs/buttons, and cape/mikroBUS-style expansion.

## Current upstream support matrix

“Reusable” means code exists, not that it is register-compatible without
validation.

| Area | Upstream QEMU baseline | Work required |
| --- | --- | --- |
| BeagleV Ahead machine | Missing | New board object, properties, DT, boot-media wiring, connectors |
| TH1520 SoC | Missing | New SoC object, address map, IRQ map, reset/power/clock topology |
| C910 identity | Missing | New thead-c910 CPU model and exact reset/ID configuration |
| RV64 IMAFDC/S/U | Generic implementation exists | Constrain to C910 behavior and test exceptions/corner cases |
| T-Head scalar ISA | XTheadBa/Bb/Bs/Cmo/CondMov/FMemIdx/Fmv/Mac/MemIdx/MemPair/Sync exist | Audit against C910 encodings and behavior |
| C910 vector | Missing upstream; this workspace has a separate XTheadVector decoder, 128-bit state, frozen v0.7.1-derived execution engine, CSRs, debug/migration support and architectural guests covering state, status, reduction and mask-overlap boundaries | Complete per-instruction and randomized differential coverage, OS context/signal/ptrace tests, XTheadZvamo evidence and physical comparison without conflating it with RVV 1.0 |
| T-Head CSRs/MAEE/PMU | C910-specific core CSR state, MAEE PTE ownership/migration, strong-order scalar alignment and instruction-access faults, C=0 AMO faults, SO vector faults, MAEE-disabled PTE-bit ignore behavior and immutable eight-region physical-PMA selection are implemented; a synthetic table validates every integration path, but the actual TH1520 values, cache/order/bus effects and PMU fidelity remain | Establish and install the TH1520 physical system map, finish CSR probes and remaining memory-attribute effects, exact counters/events and hardware comparison |
| PLIC | A dedicated C900 model now provides 240 sources, eight M/S contexts, five-bit priorities, T-Head delegation, writable pending state, trigger inputs, C900 arbitration, reset and VMState | Confirm TH1520 synthesis parameters, complete trigger/security wiring and boundary behavior on hardware |
| CLINT/timer | A dedicated C900 CLINT now models MSIP/MTIMECMP/SSIP/STIMECMP, 32-bit APB registers, no MMIO mtime, M/S privilege checks, 3 MHz time, reset and VMState | Complete migration, rollover and fault-boundary tests; compare bus-width, latching, reset-domain and clock behavior with the physical TH1520 |
| Clock/reset control | The workspace models the AP clock and reset banks, seven PLL groups, the misc-system USB/storage reset and clock bank, documented reset values/write masks, deterministic PLL locking and VMState.  All 28 mainline-described reset groups for modeled AP peripherals, all three storage groups and all three USB members drive device resets and are replayed after migration.  All 33 represented AP leaf gates and eight misc gates export reconstructed levels; PWM, timer0/1 and WDT0/1 gates pause and resume their timed consumers.  The generated DT uses the upstream Linux providers | Couple the remaining raw gates only after their device-specific bus/engine semantics are established; validate parent dependencies and split APB/core/AXI, shared-GMAC, storage and USB reset scope plus held-reset MMIO, release ordering and retention; connect hart/mailbox resets only after their sequencing is established; model remaining AO/video/DSP/misc domains and power transitions |
| UART0-5 | This workspace's reusable DW APB wrapper is integrated at all six TH1520 addresses and PLIC sources, with exact upstream clock IDs, AP reset pairs and board enablement | Verify TH1520 synthesis values, access behavior and the reserved portions of the larger apertures; complete optional shadow/DMA/RS-485 behavior, clock-gate coupling and physical reset-scope validation |
| I2C0-5 | The reusable DesignWare model now has configurable synthesis/reset identity, abort/stuck-status registers, reset and validated VMState. All six TH1520 instances have exact Linux addresses, IRQs, clocks and AP reset pairs; I2C0 carries the 4 KiB board EEPROM with 32-byte page-write wrapping, and the pinned Linux drivers complete full-image reads | Add timed TX behavior, slave/multi-master/arbitration, clock stretch and stuck recovery, DMA/SMBus, EEPROM busy/write-protect behavior, clock-gate coupling, reserved-aperture behavior and physical reset-scope validation |
| USB host | The TH1520 misc-system and DRD wrappers map the exact public/vendor apertures, three reset outputs and PLIC source 68 around QEMU's DWC3/xHCI host.  One paired USB2/USB3 connector supports DMA, commands, IRQs, HID hotplug, migration and upstream-Linux keyboard enumeration through a test-only glue module | Replace provisional DWC3/xHCI synthesis values after hardware reads; add gate/domain fidelity, PHY/link/timing and stress/error coverage, suspend/resume, device/OTG/role/VBUS/ID behavior and Fastboot/BootROM integration; establish a production mainline glue binding/driver |
| SD/eMMC | A reusable DWC MSHC wrapper and all three TH1520 instances provide SDHCI v4.20, vendor/PHY state, PIO, SDMA, v4 64-bit ADMA2, Auto CMD23, IRQ/reset/migration, eMMC unit 0 and microSD unit 1; the three active-low misc-system storage reset groups drive isolated controller resets, and mainline Linux probes them with 64-bit ADMA.  Unit 0 opts into the synthetic eMMC 5.1/1.8 V HS200/HS400 speed profile and CMD21 contract; generic eMMC remains unchanged.  A pinned portable Linux gate mounts the modeled eMMC as root, writes/syncs/hashes 1 MiB, remounts read-only and verifies the data from a fresh QEMU process | Pin and boot an official image and vendor U-Boot; add filesystem/block stress, `e2fsck`, cache-eviction/power-loss boundaries and a stable partition identifier; validate physical CID/CSD/EXT_CSD, voltage and electrical tuning/timing; add CQE/ADMA3, boot partitions/RPMB, SDIO Wi-Fi, removable-card GPIOs, error/tuning injection and mask-ROM storage boot; validate split reset members and held-reset behavior |
| Ethernet | A reusable DWC GMAC 3.x model now provides descriptor DMA, IRQs, FCS, Clause 22 MDIO, a configurable PHY and VMState; both TH1520 instances and their APB glue are integrated, individual and shared AP reset groups drive resets, and mainline Linux binds GMAC0 as DWMAC1000. Receive filtering covers MAC0 plus 31 enable-controlled perfect addresses, byte masks and source selection; promiscuous/receive-all, broadcast/multicast, inverse and four control-frame modes; 64-bin unicast/multicast hashing; C-/S-VLAN exact, VID-only and inverse matching; and final-descriptor DA/SA/VLAN status. TH1520 additionally enables Type-2 RX status for a bounded IPv4/IPv6 TCP/UDP/ICMP subset. The shared transmit COE now honors TXCOESEL, TSF and first-descriptor CIC0-3, inserts IPv4 header and IPv4/IPv6 TCP/UDP/ICMP checksums through one C-VLAN or ESVL-enabled S-VLAN, preserves guest source buffers, and writes terminal enhanced IHE/IPE/ES status | Validate the physical 32-entry/64-bin synthesis and exact filter, VLAN, control-frame and pause semantics; establish whether VLAN hash exists before implementing it; complete RX drop/forward threshold policy and malformed/zero-checksum corners; compare TX CIC2, CIC1-on-IPv6, malformed-length/status, trailing/padding, FIFO/PBL recovery, threshold, fragment/extension and stacked-VLAN behavior; add PTP/MMC/WOL/EEE, flow control, RTL8211F vendor pages/delays/IRQ/reset, traffic stress and error injection; validate individual/shared reset boundaries and whether they cover the embedded QEMU PHY |
| SPI/QSPI | A reusable DW APB SSI master is integrated at the Linux-described SPI0 node with its AP reset pair. The pinned mainline DT/driver tree supplies no QSPI controller node or programming contract, so QSPI/XIP is deliberately not inferred from clock/reset names alone | Validate the TH1520 synthesis, reset split and board wiring; add QSPI/XIP only after a public or hardware-established controller/flash contract exists |
| PWM | A six-channel TH1520 PWM controller is integrated at ``0xffec01c000`` with its Linux binding, AP clock ID 51, aligned 32-bit control/period/falling-point registers, continuous normal/inverted waveforms, boundary-latched reconfiguration, reset and VMState.  Its AP gate drives a provisional 125 MHz/zero QEMU clock; gating freezes the pending phase and output, including across migration, and re-enabling resumes it.  It exposes test-only QOM outputs and resets immediately when either known AP PWM reset bit is asserted; the board has no generated PWM consumer | Validate reset/register/strobe semantics, physical clock rate and gate phase/output behavior, one-shot/inactive behavior, the rest of the 16 KiB aperture, pinmux/header routing and safe physical electrical behavior |
| GPIO/pinctrl/LEDs | A reusable one-port DW APB GPIO model and all six Linux-described TH1520 banks now provide 157 lines, exact IRQ/clock/DT wiring, edge/level interrupts, reset and VMState.  AP reset pairs drive GPIO0-3; GPIO4 and AO GPIO remain in their separate domains.  Five blue user-LED objects consume GPIO4 pins 8-12 and one green power LED remains on; QOM, reset, migration and Linux ``gpio-leds`` tests cover them.  All three TH1520 pad controllers provide software-visible PADCFG/MUXCFG state, exact apertures/clocks, digital reset values/write masks and VMState; both AP pad controllers have reset wiring, and the board DT includes exact GPIO ranges and LED/GMAC0/UART0/Wi-Fi groups | Validate GPIO synthesis IDs, direction wording and debounce timing, split reset/domain behavior, LED polarity/brightness/defaults and electrical effects on hardware; add remaining GPIO consumers, buttons, mux-driven signal routing and deterministic header/device backends |
| APB timers | A reusable four-counter DesignWare model and both TH1520 components now provide eight 125 MHz countdown channels, PLIC sources 16-23, local/aggregate EOI and status, reset and VMState.  Four named toggle outputs per component change at expiry; user-defined PWM mode alternates ``LoadCount`` low intervals with ``LoadCount2`` high intervals, including live second-load updates.  Their two AP leaf gates freeze and resume the corresponding enabled count and output phase, and migration preserves the active half-cycle; either known APB/core reset bit immediately resets its component.  The outputs remain test-only and all eight upstream-DT nodes remain board-disabled | Validate component synthesis, physical clock/gate semantics, access widths, initial/reload/zero/enable edges, optional 0%/100% mode, cascade and physical output/reset-domain routing on hardware |
| PVT/thermal/voltage | A reusable MR75203 model maps the exact TH1520 common, temperature, process and voltage apertures, synthesis identity, 2 temperature sensors, 11 process detectors and 16 voltage channels.  It implements the Linux SDIF programming path, deterministic QOM environment inputs, reset and VMState; pinned Linux binds and reads all advertised temperature and voltage channels | Validate physical samples and calibration across temperature/voltage, conversion latency and DONE behavior, sample-counter edges, alarm/timer/register semantics, any interrupt route, access widths, clock/reset coupling and actual rail-to-channel names on the owner board |
| RTC/watchdog | A reusable X-Gene-compatible RTC model provides counter/match/delayed-load, interrupt/mask/EOI, wrap, optional prescaler, reset and VMState at the TH1520 address with a 32.768 kHz input and PLIC source 74.  Its disabled DT node and a test-only prescaler-aware module let pinned Linux set/read at 1 Hz and receive an alarm.  A reusable fixed-TOP Synopsys DW APB watchdog model and both TH1520 AP instances provide countdown/restart, direct and two-stage interrupt/reset behavior, PLIC sources 24/25, independent AP resets, gated 125 MHz/zero QEMU clock links, VMState and conservative disabled DT nodes; gating freezes and re-enabling resumes a running count.  Pinned Linux binds, starts, pings and reset-stops both through an external enabling DT.  AO/audio watchdogs remain absent | Validate RTC component identity, exact prescaler/CPCVR/wrap/load edges, calibration, wake and battery/reset retention; establish a mainline TH1520 RTC compatible/driver contract.  Validate watchdog identities, physical clock/gate and reset scope plus edge behavior, and add remaining watchdog domains from public or measured evidence |
| AXI DMAC | A reusable DW AXI DMAC 1.01a model now provides four-channel direct and linked-list memory-to-memory DMA, descriptor writeback, error/IRQ state, reset and VMState; the TH1520 general instance has exact mainline-DT and AP reset-pair wiring, and the Linux driver plus `dmatest` exercise all channels | Add peripheral request/handshake wiring, secure/TEE instance, contiguous/reload/shadow/cyclic and dynamic-LLI modes, detailed fault/suspend/timing behavior, noncoherent cache effects and physical differential/reset-scope validation |
| Mailbox/system control | A bounded TH1520 mailbox model maps the four upstream-Linux resources, CPU-visible channel data/generate registers, local status/clear/mask, PLIC source 28, system reset and VMState; it deliberately has no remote CPU or firmware response | Validate the register/pulse/reset/gate behavior and add E902/C906/C910R/DSP endpoints plus their documented handoff/control protocols |
| GPU/DPU/HDMI/DSI | Matching models missing | New software-visible register/queue/display pipelines |
| NPU/camera/codec/ISP | Missing | New functional command/data-path models |
| C906/E902/DSPs | C906 CPU model is partial; E902/Q7 system integration missing | Add exact cores or execution adapters, memories, IRQs and firmware handoff |
| Security/IOPMP/eFuse | Missing | New access-control, fuse/key, TEE and secure-boot state |
| Migration | Current C910, CLINT, PLIC, AP clock/reset, UART, I2C and board EEPROM, SPI0, TH1520 PWM, APB timer, both AP watchdogs, X-Gene RTC, TH1520 mailbox, MR75203 PVT, GPIO and board-LED intensity, TH1520 padctrl, DWC MSHC, DWC GMAC, TH1520 GMAC APB glue, DW AXI DMAC, TH1520 USB misc/DRD, DWC3/xHCI, DRAM and SRAM state has VMState and focused regression coverage; established boot-critical state also has a whole-machine regression.  A focused GMAC test preserves MAC0/MAC31, frame-filter, address-hash and VLAN state and proves post-load old-address rejection/new-address acceptance using a separately created destination socket | Extend the same state inventory and boundary testing to every new controller and backend; add in-flight state if synchronous devices later gain timing, queued-packet/backend reconnection coverage for GMAC, and USB transfers active across migration |

## Workspace implementation status

The branch is being advanced in reviewable milestones rather than treating
the roadmap as a claim of completion.  At the current milestone it contains:

* a dependency-minimal ``beaglev-ahead`` machine with four ``thead-c910``
  harts, the physical RAM/SRAM/ROM map, PLIC, CLINT, all six UARTs and I2C
  controllers, both four-channel APB timer components, the bounded local-side
  TH1520 mailbox interface, the MR75203 PVT block, generated DT, and direct
  OpenSBI/kernel boot;
* C910 scalar identity, including the T-Head vendor ID and exact zero
  architecture/implementation IDs, 40-bit physical-address constraints, the
  TH1520 no-PMP configuration, Zfh and its Zfhmin dependency, the initial
  custom CSR bank, migration state, MAEE PTE ownership/translation, and
  dynamic MXSTATUS/SXSTATUS ``MM`` control of standard integer/FP plus every
  modeled scalar XThead memory path.  The MAEE page attributes survive the
  page walk: strong-order mappings require post-translation scalar alignment
  and reject instruction fetches with an access fault, while non-cacheable
  mappings remain executable.  Atomic read-modify-write operations reject a
  non-cacheable mapping while LR/SC remains valid, and XTheadVector accesses
  reject strong-order mappings, including fault-only-first truncation after a
  valid first element.  With MAEE clear, the five PTE bits are ignored as the
  RTL requires.  QEMU now provides the immutable eight-region fallback-map
  path required by the RTL, but the BeagleV Ahead machine deliberately leaves
  it invalid because the actual TH1520 ranges and attributes are not
  established.  Guarded Sv39 coverage checks these rules plus
  alignment-versus-page-fault priority in S and U modes, including delegated
  traps;
* the C9xx PMU's 16 programmable counters, raw-selector WARL rules,
  machine/supervisor overflow CSRs, delegable local cause 17, exact Linux DT
  event maps, and focused CSR/fixed-counter overflow tests.  The overflow test
  uses instruction counting so its near-wrap deadline is independent of host
  speed.  A generic duplicate-selector regression proves counters 3 and 4 can
  count the same event and that clearing one selector preserves the other; it
  fails freshly fetched upstream `master` and passes this branch.  Generic and
  C910 migration guests cover fixed and programmable counter continuity,
  Smcntrpmf filters, selector routing, active and inhibited counters, near-wrap
  and already-pending overflow state, standard and vendor overflow bits,
  `mip.LCOFIP`, acknowledgement and rearming.  A separate
  `rv64,pmu-mask=0` case proves fixed counters are preserved even when no HPM
  counters exist.  Generic Sscofpmf coverage additionally makes standard
  local interrupt bit 13 extension-aware in `mideleg`, `mie`, `mip`, `sie`
  and `sip`; proves M-mode and delegated S-mode delivery; checks AIA virtual
  aliases with present and missing prerequisites; and preserves the enable,
  delegation and pending aliases across migration.  This fixes the defect
  already tracked by upstream issue #3969 and is distinct from the C910's
  vendor cause-17 overflow interface.
  Microarchitectural event values remain an explicit hardware-differential
  task;
* XTheadVector decode/translation/helpers, 128-bit vector state, T-Head status
  and CSR behavior, debugger/migration integration, naturally aligned vector
  load/store enforcement independent of MXSTATUS.MM, source-preserving
  illegal ``th.vsetvl`` handling, and focused qtest/TCG coverage for WARL,
  ``vstart``, mask/tail, saturation, rounding and reduction-boundary behavior;
  and
* a reusable C900 CLINT derived from pinned openC910 RTL and OpenSBI behavior,
  with exact M/S software and timer banks, four-hart wiring, a 3 MHz time CSR,
  reset and migration state, qtests for every output, and a TCG privilege/CSR
  delivery test;
* a reusable C900 PLIC with the Linux-established 240-source/eight-context
  topology and public-RTL delegation, pending, priority, arbitration,
  trigger, claim/complete, reset and migration behavior, plus qtests for all
  contexts and a TCG M/S/U privilege and interrupt-delivery test; and
* a reusable DesignWare APB UART wrapper replacing the temporary 16550/
  unimplemented-region combination, with DesignWare status, software reset,
  busy detection, fractional-divisor and synthesis-probe behavior, configurable
  FIFO depth, VMState, focused RX/TX/IRQ/reset/migration qtests, and a
  guest-executed access/interrupt test.  All six TH1520 instances are mapped at
  their upstream Linux addresses and PLIC sources with distinct serial aliases
  and AP clock IDs; matching the board DT, only UART0 is enabled by default.
  Unproved shadow, DMA, and RS-485 blocks are deliberately omitted rather than
  exposed as partial features; and
* an extended reusable DesignWare I2C master with configurable synthesis and
  reset registers, exact interrupt-mask support, enabled-write restrictions,
  NACK/abort-source clearing, system reset and validated in-flight VMState.
  All six TH1520 instances are mapped at their Linux addresses on PLIC sources
  44-49 and AP clock IDs 64-69.  I2C0 is board-enabled with the
  schematic/BOM-established FT24C32A-compatible 4 KiB EEPROM at ``0x50``;
  the other five remain board-disabled.  Synthetic contents default to erased
  ``0xff``, while an exact-size raw image can provide private factory data and
  persistent writes.  Multi-byte writes wrap inside the selected 32-byte page.
  EEPROM memory and its current-address state migrate; five focused tests cover
  DT/reset/identity, all PLIC routes, repeated-start data access including a
  nonzero page boundary, backing persistence and in-flight migration.  A
  pinned Linux build with AT24 enabled binds both drivers and reads all 4096
  bytes; and
* a reusable DesignWare APB SSI master at SPI0, ``0xffe700c000``, on PLIC
  source 54 and AP clock ID 54.  Its generated ``spi0`` node has the exact
  upstream-Linux compatible strings and remains disabled, with no invented
  board child.  Standard, transmit-only, receive-only and EEPROM-read FIFO
  transfers, threshold/error interrupts, serial loopback, reset and VMState
  are functional.  The generic 16-frame FIFO, one native chip select, zero
  component ID/version and synchronous transfer timing are conservative model
  defaults, not claims about the TH1520; four focused qtests cover the
  register, PLIC, error and migration contracts.  No flash, QSPI, XIP, DMA,
  clock/reset coupling, pinmux or physical peripheral wiring is asserted; and
* a six-channel TH1520 PWM controller at ``0xffec01c000`` using the current
  Linux ``thead,th1520-pwm`` binding and AP clock ID 51.  The ``0xb0`` bytes
  used by the in-tree Linux driver accept aligned 32-bit control, period and
  falling-point accesses.  Continuous normal/inverted waveforms, boundary-
  latched configuration updates, reset, virtual-time edges and VMState are
  implemented with a provisional AP-gated 125 MHz input.  Gating freezes the
  phase and output and re-enabling resumes the pending edge.  Four focused
  qtests cover all six channel register banks, polarity/timing,
  pending-update migration and migration while gated; the direct-DT test
  covers the binding.  The model exposes six
  QOM output lines only for emulator tests: the documented AP reset pair
  immediately restores its digital reset state, but no physical pin, board
  consumer, one-shot/inactive-output behavior, physical gate semantics or
  reserved-aperture behavior is claimed; and
* a reusable four-counter DesignWare APB timer with aligned 32-bit load,
  current, control, EOI, local and aggregate status, component-version,
  second-load and protection registers.  Periodic and free-running countdown,
  masking, raw interrupt latching, reset and VMState are functional.  Two
  TH1520 components map timers 0-7 at ``0xffefc32000``/``0xffffc33000`` with
  AP-gated 125 MHz clocks and PLIC sources 16-23.  Gate removal freezes an
  enabled countdown and restoration resumes it.  Either documented APB/core
  reset bit immediately restores its component's digital reset state.  Four
  qtests cover registers, exact virtual timing, every route and migration,
  while a TCG payload checks access faults and masked/unmasked interrupt
  delivery through the PLIC.  The generated nodes remain disabled like
  upstream Linux; and
* a reusable Synopsys DesignWare APB watchdog with aligned 32-bit control,
  timeout, current-count, restart, status, EOI and component-probe registers.
  It implements sticky enable, fixed TOP values, first-period ``TOP_INIT``,
  subsequent ``TOP`` reloads, the ``0x76`` kick, direct reset mode and the
  interrupt-then-reset two-stage path.  Both TH1520 AP instances map at
  ``0xffefc30000``/``0xffefc31000`` with independent provisional AP-gated
  125 MHz clocks, PLIC sources 24/25 and active-low AP resets.  Gating freezes
  an enabled count and re-enabling resumes it.  Seven device qtests cover
  registers, exact virtual timing, both routes, reset isolation, QEMU reset
  action and two-stage migration.  Generated nodes remain disabled pending an
  upstream TH1520 DT policy; a controlled external DT proved both pinned Linux
  drivers bind, start, report time left, ping and reset-stop.  Version and
  unproved parameter values remain zero and the conflicting CCVR reset value
  remains explicitly provisional; and
* a TH1520 mailbox controller at ``0xffffc38000`` with the four exact
  upstream-Linux resources, clock IDs 72-75 and level-high PLIC source 28.
  It preserves the four channel INFO0-INFO7 and generate registers, plus the
  C910-local status/clear/mask behavior used by the upstream driver.  The
  remote-ICU0 offset quirk and the other two remote ICU windows are represented
  without inventing an E902, C906, C910R or AON firmware response.  Two focused
  qtests cover the generated DT, local register/IRQ/reset behavior and
  migration; and
* a reusable one-port DesignWare APB GPIO model with software data/direction,
  external pin sampling, edge/level interrupt control, polarity, masking, EOI,
  reset and VMState.  All six TH1520 banks are integrated at their upstream
  Linux addresses and PLIC sources with exact ``ngpios`` widths and AP clock
  IDs where applicable.  The generated DT provides gpio0-5 aliases and the
  five board LEDs on GPIO4 pins 8-12.  Five active-high blue QEMU LED objects
  consume those lines, and an always-on green object represents the power LED.
  The mainline-described AP reset pairs reset GPIO0-3; GPIO4 and AO GPIO stay
  in their separate reset domains.
  Their intensity is available as a read-only QOM property.  Focused qtests
  cover every bank, pin I/O, both interrupt modes, user-LED direction/data,
  reset and migration; the pinned Linux driver binds all six controllers and
  its standard ``gpio-leds`` driver switches all five user LEDs through sysfs;
  and
* a TH1520 pad-controller model with the three always-on and application-domain
  instances at their exact apertures and clocks.  It preserves the documented
  digital PADCFG/MUXCFG reset words, reserved-bit masks, system reset and
  VMState.  The generated DT reproduces all six Linux GPIO range mappings and
  the board's LED, GMAC0, UART0 and Wi-Fi pin groups; pinned Linux binds all
  three controllers.  The two application instances have AP reset wiring;
  the always-on instance does not.  Actual mux-driven signal routing, pad electrical effects
  and PHY/Wi-Fi/card-detect consumer wiring remain open hardware-validation
  work; and
* a reusable DesignWare Mobile Storage Host Controller wrapper with the
  TH1520's 64 KiB aperture, vendor pointers, v4.20 capabilities, vendor and
  PHY register state, deterministic power-good/DLL-lock behavior, VMState,
  and all three eMMC/SDIO instances at their physical addresses and PLIC
  sources.  Generic SDHCI now accepts v4 controllers, preserves Host Control 2,
  implements Auto CMD23 and 128-bit 64-address ADMA2 descriptors, and restores
  its interrupt output after migration.  The Ahead eMMC alone enables a
  synthetic eMMC 5.1 profile with 1.8 V HS200/HS400, legal CMD6 transitions,
  standard four-bit/eight-bit CMD21 data and SDHCI Execute Tuning consumption;
  generic eMMC behavior is unchanged.  The three active-low misc-system reset
  groups independently reset eMMC, SDIO0 and SDIO1; and
* a reusable DesignWare GMAC 3.x model, factored from the NPCM implementation,
  with normal and enhanced descriptors, 16/32-byte descriptor stride, TX/RX
  DMA, FCS handling, bus errors, interrupt recomputation, configurable version/
  feature/PHY identity and VMState.  Both TH1520 GMACs are mapped at their
  physical core/APB addresses and PLIC sources, with the nine-register APB
  reset/mask contract, separate 500 MHz AXI, 1 GHz peripheral and 125 MHz APB
  clocks from the AP clock provider, board GMAC0/RTL8211F-facing DT wiring,
  disabled board GMAC1, individual four-member reset groups and a shared AXI
  reset group.  The receive path filters before allocating an FCS buffer or
  touching an RX descriptor: MAC0 plus 31 additional perfect slots support
  enable, source-selection and byte-mask controls; the frame filter implements
  promiscuous/receive-all, broadcast/multicast, destination/source inverse and
  all four control-frame modes including processed pause recognition; the
  64-bin address hash follows the documented CRC examples; and C-/S-VLAN
  perfect, VID-only and inverse matching updates final-descriptor DA/SA/VLAN
  status.  TH1520 uses the generated-DT contract of 64 address-hash bins and
  32 total perfect entries.  VLAN-hash mode is deliberately not exposed
  because its synthesis has not been established.  An immutable machine
  property enables filtering for TH1520 while retaining the reusable model's
  legacy accept-all behavior for existing NPCM machines.  A separate opt-in
  property enables TH1520's advertised Type-2 receive status without changing
  existing NPCM descriptors.  With IPC and enhanced descriptors enabled, the
  bounded classifier validates IPv4/IPv6 and TCP/UDP/ICMP checksums, handles
  one C-VLAN or ESVL-enabled S-VLAN tag, reports fragments and unsupported
  payloads as bypassed, writes RDES4 only on the terminal descriptor, sets
  ESA/ES consistently, and preserves extension words 5-7.  The shared
  transmit COE is gated by the advertised feature and store-and-forward mode,
  latches CIC from the first descriptor, distinguishes CIC0-3, and inserts
  bounded IPv4/IPv6 TCP/UDP/ICMP checksums through one supported VLAN tag.
  It changes only the gathered outgoing frame, preserves guest DMA buffers,
  ignores bytes beyond the IP-declared payload as stuff, and reports IHE/IPE
  only on the terminal descriptor with enhanced-format ES semantics; and
* a reusable Synopsys DesignWare AXI DMAC 1.01a model with four channels,
  direct and 64-byte-LLI memory-to-memory transfers, 64-bit addresses,
  transfer-width and increment behavior, descriptor valid/last/writeback,
  block/transfer/error status, interrupt masking and aggregation, reset and
  VMState.  The TH1520 general controller is mapped at ``0xffefc00000`` on
  PLIC source 27 with the exact four-channel Linux binding and a measured
  125 MHz APB clock-provider contract plus its two-member AP reset group; and
* TH1520 AP clock and reset controllers at ``0xffef010000`` and
  ``0xffef014000`` with the documented REE register banks, seven PLL groups,
  reset values and writable masks, deterministic 21.25 microsecond PLL-lock
  delay, self-clearing calibration pulses, system reset and VMState.  An
  overdue deadline is materialized when ``PLL_STS`` is read, so a tight guest
  poll cannot outrun I/O-thread timer dispatch.  The
  clock bank exports all 33 mainline-defined leaf gates for modeled AP
  consumers.  PWM, timer0/1 and WDT0/1 also drive QEMU Clock links at 125 MHz
  when enabled and zero while gated; the other AP gates remain observable raw
  state pending consumer-specific behavior.  The miscellaneous bank similarly
  exports eight raw gate levels.  Gate outputs are reconstructed after
  migration.  The reset bank exports all 28 mainline-described groups for
  currently modeled AP children: watchdogs, PWM, timer components, UART0-5,
  I2C0-5, SPI0,
  GPIO0-3, application pad controllers, DMAC0 and individual/shared GMAC
  domains.  Assertion of any represented active-low member immediately
  cold-resets the corresponding whole QEMU device; outputs are reconstructed
  from migrated register state.  This deliberate approximation leaves MMIO
  accessible while held and does not claim split-member or PHY scope.  The
  generated DT now exposes the upstream Linux bindings and uses their real
  clock IDs for all six UARTs, the general DMAC, all three storage controllers
  and both GMACs instead of temporary fixed-clock nodes; and
* a TH1520 USB host path comprising the misc-system bank at
  ``0xffec02c000``, DRD wrapper at ``0xffec03f000`` and DWC3/xHCI core at
  ``0xffe7040000`` on PLIC source 68.  One paired USB2/USB3 connector has
  functional guest-memory DMA, command/event rings, interrupts and HID
  hotplug.  The three active-low wrapper reset inputs conservatively reset the
  complete reusable core, while the misc/wrapper/DWC3/xHCI state migrates.
  Five normal-build tests cover register masks and provisional identity,
  resets, both xHCI ERSTBA write orders, DMA/PLIC completion, keyboard hotplug
  and migration.  A pinned upstream Linux 7.2 kernel with a deliberately
  test-only TH1520 glue module enumerates the QEMU keyboard.  The generated
  DWC3 node remains disabled because mainline has no TH1520 parent binding or
  glue driver; device/OTG, PHY and recovery behavior remain open; and
* a deterministic direct-boot contract that selects hart 0 for both the
  FW_DYNAMIC relocation stage and OpenSBI's later cold-boot lottery, plus a
  four-hart M-mode payload whose ordered UART transcript proves that harts
  0 through 3 all entered the common reset path.  A supplied ``-dtb`` now
  replaces the generated tree rather than being silently ignored, with a
  marker round-trip qtest.  An explicit ``boot-mode=mask-rom`` alternative
  loads a non-empty, at-most-1-MiB user raw image at ``0xffffd00000`` and
  bypasses every direct loader and trampoline.  Tests prove ROM
  immutability, reset, same-version migration, C910 execution, repeated UART
  output, invalid mode/missing image/overflow errors and rejection of mixed
  direct-loader options.  This execution bridge does not model straps, image
  parsing, authentication, media fallback, TEE state or hart release; and
* a whole-machine migration test that moves DRAM, SRAM, per-hart base and
  C910-specific CSR state, the rotating CPUID cursor, architectural time,
  CLINT, PLIC, AP clock/reset state, distinct state in all six UARTs and all
  six I2C and GPIO controllers, the board EEPROM, five user-LED intensities,
  SPI0, both APB timer components, X-Gene RTC, TH1520 mailbox state, all three
  pad controllers and
  all three storage controllers, both GMAC cores, PHY banks and both GMAC APB-glue
  instances, plus TH1520 USB misc/DRD, DWC3 and xHCI state together.  A
  separate same-version GMAC migration regression preserves MAC0, MAC31,
  frame-filter, address-hash, VLAN, IPC and active enhanced-ring state, then
  proves that the destination rejects the old backend address, accepts the
  programmed address without consuming an extra descriptor, and produces the
  expected Type-2 RDES4/ESA result after resume.  Its destination socket is
  created separately; queued packets and backend migration are not covered;
  and
* a minimal device build that excludes unrelated boards and most unused
  devices without deleting shared source prematurely.

The C900 PLIC, CLINT and boot-critical UART portions of the Phase 1 interrupt
gate are implemented.  OpenSBI and Linux earlycon now pass with both the full
and dependency-minimal builds, and the all-hart UART payload passes.  The
Phase 1 implementation gate is now closed: the full, dependency-minimal and
ASan/UBSan builds pass the board qtests and whole-machine migration test, and
the instrumented guest payloads pass without sanitizer findings.  The exact
UART synthesis values and physical reset sequence remain hardware-validation
items rather than Phase 1 assumptions.  Phases 2 and 3 likewise retain their
exhaustive and physical-differential gates.  All provisional behavior is
linked to an open item in the companion ledger.

### Current boot-validation snapshot

Linux commit ``2709dd5ae32f0828f386327c76bba9f39f63a1c6`` was built with its
TH1520, T-Head errata/noncoherent DMA, SBI, SMP, 8250 console and DesignWare
8250 support enabled.  The same unmodified image boots under the full and
dependency-minimal QEMU builds through bundled OpenSBI, reports the BeagleV
Ahead model, brings up all four harts, uses the 3 MHz timer, and binds UART0 as
``ttyS0`` at ``0xffe7014000`` with IRQ 12 and base baud 6250000.  It then
reaches the expected panic because no root device is attached.  The log has no
noncoherent-DMA warning and no leaked UART FIFO-probe byte sequence.

The direct-boot FDT restricts OpenSBI's cold-boot allow-list to hart 0 because
all four emulated harts currently enter the common reset trampoline.  Six
repeated Linux boots in each build selected OpenSBI boot hart 0 and brought up
all four CPUs.  A separate M-mode payload serializes the four harts and checks
the exact UART transcript ``0123\n``.  This is a deterministic direct-boot
contract, not evidence for the physical reset controller or BootROM sequence.

The opt-in user-ROM path is deliberately separate.  It executes raw bytes
from the existing mask-ROM aperture and never installs the direct firmware,
kernel or FDT handoff.  Its synthetic payload and qtests cover execution,
UART, reset, immutability, BIOS-path lookup, migration and configuration
errors.  The migration test proves that source ROM bytes replace deliberately
different destination bytes and survive reset; private bytes therefore cross
the migration channel.  It still starts all four C910 harts at the same
address and supplies no strap, media, security or release-controller behavior,
so it is only the first bounded Phase-4 checkpoint.

The focused gate currently passes 113 board qtests in the normal build and 112
in the dependency-minimal build.  The sole conditional difference is the HID
hotplug test because ``usb-kbd`` is intentionally absent from the minimal
configuration.  The four remaining USB tests cover exact
misc/DRD register resets and masks, provisional DWC3/xHCI capabilities, all
three reset outputs, Linux's high-half-first and the conventional
low-half-first ERSTBA sequences, guest-memory DMA, PLIC source 68, system reset
and migration.  These gates also include ten storage tests
for the generated DT, exact controller/PHY reset and masks, all three PLIC
routes, all three misc-system reset outputs and isolated reset effects,
configurable unknown synthesis IDs, eMMC PIO read/write, SD Auto CMD23 with a
64-bit ADMA descriptor and buffer above 4 GiB, and device migration.

Three additional storage qtests account for the new totals.
``sd-cmd19-tuning`` protects the generic SD tuning pattern and rejects
wrong-size and wrong-opcode false successes.  ``emmc-hs400-profile`` checks the
synthetic EXT_CSD fields, rejected CMD6 transitions and ``SWITCH_ERROR``
lifetime, both standard CMD21 patterns, Execute Tuning IRQ/FIFO state, the
reference HS200/CMD21/HS/DDR8/HS400 workflow, enhanced-strobe rejection and
reset.  ``emmc-tuning-migration`` checks a partially consumed 128-byte tuning
FIFO, an armed Execute Tuning request and migrated HS200/HS400 card/controller
mode.  The old binary fails before the change, the normal and minimal suites
pass, and the complete 13-test storage group passes under ASan/UBSan.  The
previous complete sanitizer-board run predates these three tests and remains
recorded at 108; it also predates the later PLL-poll test and is not relabeled
as a current full sanitizer run.

The generic RISC-V CSR/migration binary now passes eleven subtests, including
fixed-only, active, inhibited and pending PMU migration plus Sscofpmf
extension-on/off WARL and alias coverage.  Six freestanding Sscofpmf variants
cover M/S interrupt delivery and AIA virtual aliases with complete, disabled
and missing-prerequisite configurations.  The complete RISC-V qtest gate
passes 17 suites with one expected skip, and the complete RISC-V TCG guest
suite passes.  Dependency-minimal and ASan/UBSan configurations each pass
their four available C910 CSR/migration subtests.  The current
dependency-minimal board gate is 112/112; the last complete sanitizer-board
run remains the explicitly historical 108/108 result above, while current
focused sanitizer CPR and storage groups pass 9/9 and 13/13.

The XTheadVector state milestone adds a second architectural payload.  A
reserved-EDIV ``th.vsetvl`` regression first failed because the translator
used its source ``TCGv`` as scratch and changed the guest ``rs2`` register to
``0xff``.  The translator now copies the operand before synthesizing an
illegal RVV-format value.  Register and immediate forms both produce
``vill``, zero ``vl`` and ``vstart`` without source corruption.  The separate
state payload proves the 128-bit ``vstart`` write mask, prestart preservation,
the ``vstart >= vl`` no-write rule, mask-undisturbed and tail-zero behavior,
sticky unsigned saturation, and all four fixed-point rounding modes.  It also
proves that ``th.vmfirst.m`` traps without changing its destination or
``vstart`` when ``vstart`` is nonzero, then checks the legal first-set and
no-set results.  The mask-query translator now enforces that legality rule.
These are specification regressions.  Both payloads park secondary harts and
pass as firmware ELFs on the four-hart machine under the normal,
dependency-minimal and ASan/UBSan builds; the complete normal TCG suite also
passes.  Physical C910 stepping behavior remains under the hardware ledger.

The XTheadVector floating-point state milestone adds a third architectural
payload.  It requires one representative from all 18 floating-point
decode-check families to trap while FS is Off and then proves those same raw
encodings decode with FS enabled.  It also rejects the unsupported e8
single-width and e64 widening reductions, and independently checks that an
integer reduction traps with VS Off.  Functional divide, square-root,
comparison and reduction cases cover all six exception-producing helper-loop
families: new DZ, NV and OF+NX flags make FS Dirty, while an exact division
leaves both ``fflags`` and FS unchanged.  This exposed and fixed missing FS/VS
legality checks, unsafe reduction widths and missing exception-state
propagation inherited from the public unmerged April 2024 XTheadVector series.
The three payloads pass as board firmware under the normal,
dependency-minimal and ASan/UBSan builds.  The complete normal RISC-V TCG
guest suite and all 17 runnable RISC-V qtest suites, including the 109-case
board suite, remain green.  Silicon NaN, exception and stepping-specific
behavior remains explicitly unverified under ``CPU-006``.

The XTheadVector reduction-boundary milestone adds a fourth architectural
payload and a separate standard-RVV illegal-width payload.  It proves that
integer and floating-point reductions leave all destination bytes unchanged at
``vl=0``, trap for every nonzero ``vstart``, and zero their tail only after a
nonempty operation.  It also checks all three e64 widening source-shape
classes, a misaligned LMUL=8 source group, valid integer and floating-point
LMUL=8 reductions with scalar input/output operands, destination overlap with
the source or mask at the permitted LMUL, and an all-inactive mask.  The
translator now validates before indexing its widening helper tables; the
matching standard-RVV fix is tracked separately as upstream candidate
``UQ-013``.  The normal, dependency-minimal and ASan/UBSan board runs pass,
as do all 26 normal RISC-V softmmu TCG guests and all 17 runnable RISC-V
qtest suites.  This confirms the frozen specification contract, not C910
silicon behavior; physical results remain required under ``CPU-006``.

The XTheadVector overlap milestone adds a fifth architectural payload.  The
frozen extension explicitly forbids an LMUL-greater-than-one comparison
destination from overlapping any source group, including the implicit mask
source ``v0``; the inherited v0.7.1 rule applies the same LMUL boundary to
masked mask-prefix destinations.  The fail-before guest exited at its first
stage because masked LMUL=2 ``th.vmseq.vv`` executed.  Four shared comparison
checkers and the mask-prefix translator macro now apply the existing
``th_check_overlap_mask`` rule.  The guest requires exact illegal-instruction
traps for integer VV/VX/VI and floating-point VV/VF comparisons plus
``th.vmsbf.m``, ``th.vmsif.m`` and ``th.vmsof.m``.  It proves that a nonzero
``vstart`` and all 16 bytes of ``v0`` survive the first trap, while LMUL=1,
unmasked LMUL=2 and masked non-``v0`` controls remain legal.  Normal,
dependency-minimal and ASan/UBSan runs pass, as do all 27 normal RISC-V
softmmu TCG guests, 109 normal board qtests, and 108 board qtests in both the
dependency-minimal and sanitizer builds.

The 2026-08-24 C910 alignment milestone also passes the complete normal-build
RISC-V softmmu TCG suite.  Its dedicated M-mode payload toggles
MXSTATUS.MM and the SXSTATUS.MM alias across TB boundaries; exercises
misaligned integer, double/word/half floating-point, XTheadMemIdx indexed and
incrementing, XTheadMemPair and XTheadFMemIdx loads/stores; and requires
standard atomics plus XTheadVector loads/stores to remain naturally aligned.
A second payload builds explicitly cleared Sv39 page tables with one mapped
guard page followed by an unmapped page.  It runs S-mode faults through the
M-mode handler and delegated U-mode faults through the S-mode handler, and
checks 23 exact traps, trap values, mapped misaligned scalar success, aligned
missing-page faults, second-page scalar fault priority with MM set,
misalignment priority with MM clear, atomic/vector priority independent of MM,
and no visible first-page bytes from the tested faulting word store.  The full
normal TCG suite passes.  Both alignment payloads pass in the
dependency-minimal build; the new guarded-page payload also passes ASan/UBSan
with only QEMU's expected coroutine warning.  The
aggregate minimal TCG target is intentionally inapplicable because it begins
with tests for the omitted generic ``virt`` machine; the explicitly enumerated
board-compatible subset is the pruning gate.

The following MAEE milestone is grounded in openC910 RTL commit
``b91c90914c19f114d35c8f6b73408eb241ed847c`` and mainline Linux's T-Head
memory-type encodings.  QEMU carries PTE bits 63:59 through the RISC-V page
walk and uses the strong-order bit to request page-dependent natural alignment
from the TCG TLB.  RISC-V uses the alignment-aware TLB-fill hook so the first
access to a new mapping and a cross-page access cannot bypass that rule.  A
data-side fill suppresses executable permission for a strong-order mapping,
ensuring a later instruction fetch re-walks and raises the required
instruction access fault.  The RTL page-fault expression does not reserve
PTE[63:59]: when MXSTATUS.MAEE is clear, ``ct_mmu_ptw.v`` ignores them and
selects synthesis-specific physical-system-map flags.  QEMU now matches that
selection with an immutable eight-region table plus default.  Direct M-mode,
MMU-disabled, Bare and final translated physical-address paths share one
lookup; MAEE-enabled PTE attributes retain precedence.  An unconfigured table
remains explicitly invalid, so the BeagleV Ahead machine does not invent the
actual TH1520 ranges while authoritative integration evidence or hardware is
still absent.

The freestanding payload constructs normal, non-cacheable, strong-order and
non-shareable aliases and checks M-owned MAEE transitions, S-mode traps,
delegated U-mode traps, exact trap values, first/second-page attribute
asymmetry, data-to-instruction TLB reuse and the MAEE-disabled PTE-bit rule.
It requires C=1 for 32- and 64-bit AMO read-modify-write operations without
changing LR/SC, and rejects strong-order XTheadVector loads and stores at 8-,
16-, 32- and 64-bit element widths.  Unit-stride, strided, indexed and
two-field segment forms are covered.  Segment fault-only-first loads trap when
field one of element zero is denied and otherwise shorten ``vl`` before a
later strong-order segment.  The payload parks secondary harts, checks 31
exact traps, and distinguishes the legacy vector-store encoding from its
overlapping ratified-RVV encoding.  It passes in the normal,
dependency-minimal and ASan/UBSan builds together with the older MXSTATUS.MM
and guarded-priority payloads; generic Zicclsm enabled/disabled coverage also
remains green.  The complete normal RISC-V TCG suite passes.  The normal qtest
gate passes 109 board and eleven CSR subtests; minimal and sanitizer each pass
their 108 available board and four CSR subtests.  QEMU still does not claim
cache, buffering, shareability, security-bus or actual memory-order effects.

A separate physical-PMA payload uses only an explicitly experimental
synthetic CPU configuration.  Its custom linker places pages immediately
below and at every one of eight upper boundaries and in the default region.
It checks 38 exact traps and successful counterparts across direct M mode,
S-mode Bare, S/U Sv39, MAEE on/off and first/cached TLB paths.  It proves
physical SO scalar/vector/fetch restrictions, C=0 AMO.W faults with LR.W/SC.W
still allowed, PTE precedence and physical fallback.  Normal,
dependency-minimal and ASan/UBSan focused runs pass.  These values are test
fixtures, not a proposed TH1520 map.

Ten GMAC-path tests cover the exact DT/clock/APB/MDIO contract, masked APB
writes, both PLIC routes, enhanced 32-byte TX/RX descriptors, FCS,
extension-word preservation, and socket-backed packet paths.  They include a
deterministic positive-barrier rejection regression, a 34-case matrix for
perfect/hash/source/broadcast/multicast/control/VLAN acceptance and descriptor
status, a Type-2 classifier matrix plus split-descriptor boundary, and focused
same-version migration of filter/IPC state followed by an old-address drop,
new-address accept and post-resume RDES4 result.  The matrix covers valid and
bad IPv4/UDP, IPv6/TCP, truncated IPv6 extension, fragment, non-IP, C-/S-VLAN,
truncated VLAN and IPC/ATDS-disabled controls.  A separate independent-oracle
matrix exercises 18 transmit cases: CIC0-3, IPv4/IPv6 TCP/UDP/ICMP, options,
hop-by-hop, one C-/enabled S-VLAN, split descriptors and buffers, TSF bypass,
UDP zero, trailing stuff, a mismatched UDP length, short payloads, malformed
headers, terminal status and guest-buffer preservation.  The seven-case NPCM
suite separately covers normal-descriptor CIC3 IPv4/UDP compatibility and
retains its legacy Type-2 receive contract.  Four DMAC tests cover
reset/masks, a direct copy and PLIC route, a two-item LLI chain above 4 GiB,
invalid-descriptor failure and completed-state/IRQ migration; the direct-boot
test also checks the complete generated binding and AP clock-provider IDs.
Nine AP clock/reset tests cover reset values, writable masks, all 28 exported
reset groups, per-device reset/isolation, asserted-line migration, PLL restart
and lock delay, calibration self-clear, all 33 AP leaf-gate outputs, functional
PWM/timer/watchdog gating, system reset, gate migration and reset-bank
migration while a PLL lock is pending.  The ninth executes a Linux-style
single-threaded-TCG poll and checks lock visibility before and after reset.
Two miscellaneous-system clock tests
cover all eight raw outputs, isolation, reset and migration reconstruction.  A
six-instance UART test verifies the exact addresses, aperture descriptions,
clock IDs, serial aliases, board
enablement, independent PLIC sources, scratch state and system reset.  Both
the focused UART migration test and whole-machine migration preserve distinct
state in every instance.  Three GPIO tests verify the six exact parent/port
DT nodes, gpio0-5 aliases,
five GPIO4 LED descriptions, bank widths and reset masks, pin input/output,
all six PLIC routes, rising-edge and active-low-level behavior, masking/EOI,
and migration of distinct state in every bank plus a pending edge.  Two
focused pad-controller tests and the direct-DT test verify all digital resets,
representative writable and reserved masks, system reset, distinct
three-instance migration state and the complete clock, GPIO-range and board
pin-group DT contract.  The pinned kernel binds all six GPIO and all three
pinctrl devices.  Five I2C tests verify all six exact DT/register/clock/PLIC
instances, TH1520 synthesis-visible resets, enabled-write rules,
repeated-start EEPROM access, NACK aborts, persistent raw-image backing, and
migration of a queued receive byte plus EEPROM data/address state.  A pinned
Linux kernel with the AT24 driver built in binds I2C0 and reads all 4096
default-erased bytes.  Four APB timer tests cover resets and writable masks,
periodic/free-running timing, masking/EOI, all eight PLIC routes and migration
of a running countdown with latched interrupt.  Seven watchdog tests cover
both exact DT resources, reset/probe registers, initial and subsequent timeout
stages, EOI and magic-restart semantics, both PLIC routes, independent AP
resets, QEMU reset action and migration of a pending and a running instance.
Four SPI0 tests cover generic
reset/masks, loopback and receive-only operation, PLIC delivery and
overflow/underflow clearing, plus migration; the direct-DT test checks its
disabled Linux-compatible node, address, interrupt, clock ID and alias.  A
three-test TH1520 PWM group checks all six reset register banks, normal and
inverted virtual waveforms, glitch-free boundary updates and migration of a
running waveform with a pending update; the direct-DT test confirms the Linux
binding and that no ``status`` property is present.  Two MR75203 tests cover
the exact four-resource DT contract, synthesis identity, Linux SDIF command
sequence, positive and negative temperature conversion, voltage and process
samples, sample counters, reset and migration of both guest registers and the
QOM environment inputs.  A pinned Linux build binds the generated PVT node
and reads two temperatures plus all 16 voltage channels.  A
boot test also proves that
an externally supplied DTB reaches the firmware handoff.  The
complete gate includes whole-machine migration; C910 CSR identity tests;
XTheadVector, PMU, CLINT, PLIC, UART and four-hart guest payloads; and an
S-mode SBI identity probe.  The instrumented Linux run
selects hart 0, brings up all four CPUs and probes the C900 PLIC before its
bounded timeout.  Native DesignWare UART handoff and the expected missing-root
panic are separately established by the normal full and minimal boots.  This
snapshot proves only the boot-critical interfaces exercised by those tests;
it does not resolve a hardware-only ledger item or imply stock-image
compatibility.

A separate Linux build from the same pinned commit enabled DWC3 host, xHCI and
HID in the kernel and used an out-of-tree, test-only TH1520 glue module whose
sequence is explicitly attributed to vendor kernel commit
``b9cf70c75d2b7482195a94e754d59f8cfc9dda2c``.  With an external enabling DT
and ``usb-kbd`` pre-attached, it reported:

```text
USBTEST_INIT: PASS upstream Linux enumerated QEMU USB keyboard 0627:0001 through TH1520 DWC3/xHCI
```

This proves the upstream host-driver, wrapper, DMA, interrupt and enumeration
path.  It does not promote the validation module into a production Linux
driver or resolve any PHY, role, synthesis, reset-domain or physical-board
uncertainty.

With a blank 64 MiB image attached as storage unit 0, the same pinned Linux
kernel binds all three ``thead,th1520-dwcmshc`` nodes, reports 64-bit ADMA, and
enumerates the image as ``mmc1: new HS400 MMC card`` in both the normal and
dependency-minimal QEMU builds.  The Linux host number is probe-order
dependent and is not the QEMU drive unit.  A command trace records HS200, HS,
DDR8 and HS400 CMD6 updates but no CMD21.  The pinned TH1520 callback explicitly
skips CMD21 during HS400 preparation, so this gate proves EXT_CSD negotiation,
CMD6 transitions and HS400 block discovery, not QEMU's tuning data or IRQ path.

A separate portable Linux 6.11.9 functional test uses a pinned ext2 rootfs and
``maxcpus=1``.  It mounts the eMMC-backed root, reaches a controlled root
shell, writes and syncs a deterministic 1 MiB payload, checks its SHA-256,
remounts read-only, closes QEMU, then verifies the hash from a new QEMU
process.  Both normal and dependency-minimal builds pass.  This is one bounded
filesystem path, not ``e2fsck``, block stress, cache eviction, power-loss
durability, SMP, normal distro init or official-image coverage.  CQE, SDIO
Wi-Fi, physical-card GPIOs and mask-ROM boot behavior remain unproved.

The same pinned Linux source was also rebuilt in a separate output directory
with ``CONFIG_STMMAC_ETH``, ``CONFIG_STMMAC_PLATFORM`` and
``CONFIG_DWMAC_THEAD`` built in.  It binds GMAC0 at ``0xffe7070000`` without a
probe error or clock-divider warning, reads user/version ID ``0x10/0x37``, and
selects DWMAC1000, RGMII, Type-2 RX checksum, TX checksum insertion,
enhanced/extended descriptors and ring mode.  This establishes the mainline
driver/register contract.  The focused qtest now also establishes QEMU's
deterministic CIC0-3 transmit contract, including IPv6, but no Linux traffic
run or physical capture has compared that contract with silicon.  This does
not establish a working physical link, the provisional hardware-feature
aggregate, RTL8211F vendor behavior, or any traffic/stress requirement in P5.

The pinned Linux source, with ``CONFIG_DMATEST=y``, also binds the general
controller as ``dw_axi_dmac_platform`` with four channels.  A tiny initramfs
configured one thread on each of ``dma0chan0`` through ``dma0chan3`` and ran
20 randomized memory copies per channel with buffers up to 1 MiB.  All 80
transfers completed with zero verification failures.  This exercises the
mainline driver, linked descriptors, shared interrupt and data path, but it
does not prove peripheral handshakes, cache incoherency failures or physical
timing.

The pinned Linux kernel also binds the generated ``thead,th1520-clk-ap``
provider and reports the modeled reset tree: 300 MHz CPU PLLs, a 1 GHz GMAC
PLL, 500 MHz CPU-system AXI, 125 MHz peripheral APB, 100/125 MHz UART baud/APB
inputs and 198 MHz eMMC/SDIO.  A second build with ``CONFIG_RESET_TH1520=y``
binds ``ffef014000.reset-controller`` to the upstream ``th1520-reset`` driver.
This validates the software-visible provider contracts and digital reset
state; it does not prove physical cold-reset defaults, analog PLL behavior or
that clock gates and reset outputs affect child devices correctly.

The pinned source was rebuilt with ``CONFIG_DW_WATCHDOG``,
``CONFIG_WATCHDOG_SYSFS`` and ``CONFIG_RESET_TH1520``.  A generated external
DT enabled the otherwise board-disabled WDT0/WDT1 nodes without changing their
addresses, level-high PLIC sources 24/25, clock IDs 76/77 or reset IDs 3/4.  A
freestanding init verified both Synopsys watchdog-class devices bound, opened
and started, reported positive time left, accepted the magic-close keepalive,
and stopped through their respective reset controls.  This proves the
mainline software contract against QEMU; it does not prove the physical 125
MHz rate, synthesis IDs, reset scope or board enablement policy.

The same Linux 7.2 source exposes a separate timer integration limitation.
Its generic DW APB OF code normally creates the first enabled node as a
clockevent during ``time_init()``, before the RISC-V PLIC IRQ domain exists,
and its referenced TH1520 clock provider is also not ready then.  These facts
make simple mainline enablement unusable and are consistent with the upstream
TH1520 nodes remaining disabled, but do not establish the original reason.
For a controlled QEMU-only validation, an external DT enabled timer0,
provided ``clock-frequency = <125000000>``, and a temporary uncommitted kernel
change selected the standard driver's clocksource path first.  Linux then
listed ``timer`` as an available clocksource and booted through a freestanding
init; the independent TCG payload proves the IRQ/PLIC path.  The test kernel
changes were reverted, and both limitations remain recorded rather than
hidden by enabling the generated nodes.

The same pinned Linux kernel, with ``CONFIG_GPIO_DWAPB=y``, binds all six
generated ``snps,dw-apb-gpio`` platform devices at
``0xffec005000``, ``0xffec006000``, ``0xffe7f34000``, ``0xffe7f38000``,
``0xfffff52000`` and ``0xfffff41000``.  A freestanding initramfs probe checks
the six driver links in sysfs after all four harts start.  This establishes
the mainline binding and modeled register/IRQ contract; it does not establish
physical pulls, pinmux, electrical routing, debounce timing, synthesis IDs or
GPIO-connected peripheral reset/wake behavior.

The same Linux commit, rebuilt with ``CONFIG_LEDS_GPIO=y``, binds the five
generated ``gpio-leds`` children as ``led1`` through ``led5``.  A freestanding
initramfs first confirms that every brightness value is zero, turns all five
on, then selects the alternating 1/3/5 pattern through sysfs.  QEMU's LED
trace records the corresponding five blue ``USR0``-``USR4`` transitions and
the guest reports:

``LEDTEST_INIT: PASS Linux gpio-leds drove five blue user LEDs through GPIO4[8:12]``

This proves the end-to-end Linux DT, GPIO-driver and QEMU LED connection.  It
does not replace the owner-board comparison required by ``BOARD-002``.

## Intended source architecture

Keep reusable IP independent from the board:

* target/riscv: C910 CPU definition, custom CSR state and XTheadVector decode/
  translation/helpers.
* hw/riscv/th1520.c and include/hw/riscv/th1520.h: SoC composition, address and
  interrupt maps, CPU clusters, SRAM/BootROM, system buses.
* hw/riscv/beaglev_ahead.c: board RAM, boot straps, storage, PHY/module,
  connectors, LEDs/buttons and machine properties.
* hw/intc, hw/char, hw/i2c, hw/gpio, hw/timer, hw/dma, hw/sd, hw/net, hw/usb,
  hw/display, hw/audio and hw/misc: reusable device models rather than private
  implementations hidden in the machine file.
* configs/devices/riscv64-softmmu/beaglev-ahead.mak: the dependency-minimal
  device selection.
* tests/qtest and tests/tcg/riscv64: register, IRQ, DMA, reset, migration, CPU,
  and instruction tests.
* tests/functional: complete boot tests with pinned redistributable artifacts.

Use QOM child objects and named clocks/resets/links.  Do not use broad arrays
of anonymous unimplemented MMIO regions to make drivers appear to probe.  A
temporary stub is allowed only when it is fail-loud, opt-in, traced, listed in
the ledger, and has a removal gate.

The machine name will be beaglev-ahead and the application CPU type will be
thead-c910.  The default machine RAM is 4 GiB.  A compatibility property may
permit other RAM sizes for testing, but the generated DT must describe the
actual selection.  QEMU will generate a hardware-matching DT and also accept a
user-supplied DTB for exact vendor-image reproduction.

## Execution roadmap and acceptance gates

Each phase ends with tests and a reviewable commit series.  A later phase may
start early when independent, but no gate is declared complete from a boot log
alone.

### Phase 0 — reproducible baseline and evidence

Deliver:

* this plan and the uncertainty ledger;
* exact source/image/toolchain hashes and a reference manifest;
* a riscv64-softmmu build with no source changes and captured machine/CPU lists;
* scripts that fail on timeout and archive serial logs, QEMU command line, DTB,
  register traces and exit status;
* an initial official-image inventory: partition table, SPL/U-Boot/OpenSBI,
  DTBs, kernel config and root filesystem;
* a no-hardware baseline; all hardware-only facts remain pending.

Gate P0:

* qemu-system-riscv64 builds from a clean checkout;
* the smoke harness proves upstream has neither beaglev-ahead nor thead-c910;
* every reference is pinned and provenance recorded.

### Phase 1 — minimal TH1520/board skeleton

Deliver:

* TH1520 and BeagleV Ahead QOM types, Kconfig and Meson wiring;
* four temporary generic RV64 harts at the correct reset vector;
* 40-bit physical map, 4 GiB board RAM, on-chip SRAM and a small test ROM;
* PLIC and CLINT wired at 0xffd8000000 and 0xffdc000000;
* UART0 at 0xffe7014000 with IRQ 36, reg-shift 2, 32-bit access;
* generated DT, direct firmware/kernel loading, SMP release path;
* explicit unimplemented-device diagnostics for all mapped-but-absent blocks.

Gate P1:

* qtests prove map boundaries, overlap absence, reset values, timer frequency,
  all four software/timer interrupts, and selected PLIC routing;
* a minimal M-mode payload prints on UART0 from all four harts;
* OpenSBI reaches its console and an instrumented Linux reaches earlycon;
* ASan/UBSan and migration-state tests show no new errors.

Status: passed for the direct-boot skeleton.  The gate is covered in the full,
dependency-minimal and ASan/UBSan builds.  The sanitizer reports its standard
``makecontext``/``swapcontext`` support warning but no ASan or UBSan fault.
Hardware-only reset, clock and synthesis questions remain open in the ledger.

The temporary generic CPU is a bring-up device only; it is not evidence of C910
compatibility.

### Phase 2 — exact C910 scalar architecture

Deliver:

* thead-c910 CPU identity, four-core cluster properties, Sv39 and 40-bit
  physical address behavior;
* exact supported scalar ISA and privilege version;
* C910 custom M/S/U CSRs, reset values, privilege checks, masks, WARL/WPRI
  handling and exception behavior;
* MAEE and memory-attribute effects needed by real firmware;
* PMP count/behavior, PMU counters/events, interrupt/security extensions needed
  by software, cache/CMO architectural effects and hart reset-vector behavior;
* VMState for mutable CPU-specific state.

Gate P2:

* decoder tests cover every scalar custom instruction and illegal encoding;
* CSR tests cover every bit and privilege combination, including migration;
* page-table/PMP/MAEE tests cover permissions, misalignment, aliases and faults;
* Linux reports the intended CPU/ISA without command-line workarounds;
* differential results match a reference interpreter or hardware for every
  software-visible case in the test corpus.

Status: in progress.  CPU identity, the TH1520 no-PMP/40-bit configuration,
the initial custom CSR/PMU/MAEE state, scalar XThead decode, Zfh/Zfhmin, and
MXSTATUS/SXSTATUS.MM scalar alignment behavior are implemented and covered by
CSR, migration and guest-executed tests.  Generic and C910 PMU migration
payloads now preserve Smcntrpmf configuration, fixed and programmable counter
continuity, rebuild selector routing and restore active, inhibited, near-wrap
and already-pending states without guest reprogramming on the destination.
They verify exact inhibited values, standard and vendor overflow state,
`mip.LCOFIP`, acknowledgement and a second overflow after rearming.  A
fixed-only `rv64,pmu-mask=0` payload uses privilege filtering to prove the
callbacks run when no programmable counters exist.  Standard Sscofpmf bit 13
is now extension-aware across machine, supervisor and AIA virtual-interrupt
CSRs; focused guests cover M/S delivery, acknowledgement, extension-disabled
WARL behavior, missing AIA prerequisites and pending-state migration.  The
alignment tests are
grounded in pinned openC910 RTL and distinguish scalar, atomic and vector
behavior across M/S/U privilege, delegated traps and a mapped/unmapped page
boundary.  MAEE tests additionally distinguish normal, non-cacheable,
strong-order and non-shareable mappings, enforce post-translation scalar
alignment on the
strong-order type, require a strong-order instruction access fault, reject
AMO.W/AMO.D RMW on C=0 while allowing LR.W/SC.W and LR.D/SC.D, reject
strong-order vector accesses across all four element widths and the unit,
stride, index and two-field segment paths, and cover segment
fault-only-first behavior.  When MAEE is clear, PTE[63:59] is ignored and the
new eight-region integration path supplies physical attributes for direct,
Bare and Sv39 accesses.  A 38-trap synthetic M/S/U test covers every boundary,
the default and PTE-versus-physical precedence; the real TH1520 boundaries and
attributes remain open.
The scalar-legality payload additionally executes every ``th.addsl`` immediate
and every XTheadCmo encoding in M/S/U, checks exact illegal-instruction PCs,
values and destination preservation, exercises THEADISAEE and UCME in both
directions, and observes 64 expected traps.  Its XTheadBa-disabled companion
checks four further traps while XTheadCmo keeps the shared decoder active.
All four XTheadSync instructions now emit a full sequentially consistent TCG
memory barrier before the existing translation-block exit.  A three-hart
store-buffering payload checks 4,096 trials for each instruction under MTTCG,
alongside M/S/U legality and THEADISAEE gating.  A deterministic translation-
IR check requires the barrier, PC advance and TB exit for every variant and
fails against the preserved pre-fix binary.  This establishes QEMU's
architectural inter-vCPU ordering contract; physical ordering strength,
latency, cache-operation completion and the ``.i`` pipeline/refetch behavior
remain silicon-comparison items.
P2 remains open for exhaustive scalar/illegal decode, all custom-CSR and
privilege combinations, B/SH/SEC effects, remaining scalar/FP/masked/vector
forms and boundary combinations, cache/CMO and remaining ordering effects,
physical-map provenance and migration/reset refills, reset-vector/security
behavior, randomized differential testing and physical comparison.

### Phase 3 — XTheadVector / Vector 0.7.1

Deliver:

* an explicit XTheadVector feature, never mislabeled as standard RVV 1.0;
* the six vector CSRs, decode tables, 128-bit VLEN, element/LMUL rules,
  tail/mask semantics, fixed-point flags and exception state;
* all arithmetic, widening/narrowing, mask, permutation, reduction,
  floating-point, load/store, segment, indexed and fault-only-first operations;
* debugger, disassembler, signal/migration and GDB register exposure;
* coexistence rules that prevent incompatible RVV 1.0 combinations.

Gate P3:

* one positive and boundary/illegal test per instruction form;
* randomized differential vector testing over all SEW/LMUL/vl/mask modes;
* Linux context switch, ptrace, signal and process tests preserve full vector
  state;
* known C910 vector binaries run unchanged and match hardware/reference output.

Status: in progress.  The vendor-derived execution engine, 128-bit state,
custom CSRs, migration/debug integration and a discriminating architectural
smoke test are present.  Illegal register-form ``th.vsetvl`` now preserves its
source while producing the required ``vill``/zero-``vl`` state.  A second
payload covers ``vstart`` WARL/prestart/early-exit behavior, mask-undisturbed
and tail-zero results, sticky saturation and all four fixed-point rounding
modes.  It also requires ``th.vmfirst.m`` to trap at nonzero ``vstart`` while
preserving the scalar destination and ``vstart``, and covers its legal
first-set and no-set results.  A third payload covers FS-Off legality across
every floating-point decode-check family, exception-driven
``fflags``/FS-Dirty propagation through
all six helper-loop families, no-exception state preservation, VS-Off
reduction legality and unsupported floating-point reduction widths.  A fourth
payload covers integer/FP reduction ``vl=0`` whole-register preservation,
nonzero-``vstart`` traps, all e64 widening source-shape traps, LMUL=8 scalar
reduction operands, source/mask overlap and inactive-mask/tail results; a
separate generic RVV guest covers the same e64 widening decode boundary on
``rv64,v=true``.  A fifth payload checks the implicit ``v0`` overlap boundary
at LMUL=2 across integer and floating-point comparison forms and all three
mask-prefix instructions, including destination/``vstart`` preservation after
the first trap and legal LMUL=1, unmasked and non-``v0`` controls.  Vector
loads/stores now enforce natural alignment
independently of MXSTATUS.MM, matching the pinned openC910 LSU rule; ordinary
guarded-page vector load/store priority is covered in S and U modes.  Standard
RVV translation is explicitly gated on Zve32x so overlapping store encodings
reach the legacy decoder.  MAEE coverage includes strong-order faults at every
element width through unit-stride, strided, indexed and two-field segment
paths, plus segment fault-only-first crossings that distinguish element-zero
traps from later-element ``vl`` truncation.  P3 is not closed:
the remaining per-instruction/mask/``vstart`` combinations, randomized
differential testing, broader segment/index/fault-only-first page-priority
combinations, corresponding scalar floating-point extension/property
combinations, OS context/signal/ptrace coverage, XTheadZvamo availability and
physical-silicon comparison remain open.

### Phase 4 — authentic reset and boot

Deliver:

* BootROM behavior for USB Fastboot, eMMC, SD, SPI NAND/NOR and UART CCT modes;
* BOOT_SEL and flash-page-size strap properties;
* real reset sequencing, initial TEE/core state, secondary hart release,
  retention SRAM and mailbox handoff;
* image parsing/authentication behavior that is legally and technically
  reproducible, with a user-supplied BootROM option if redistribution is not
  possible;
* QSPI and the minimum clock/reset/system-control blocks used before DRAM.

Current bounded checkpoint: a user-supplied raw mask-ROM image can execute
from the correct ROM aperture without QEMU's direct-boot trampoline.  This
unblocks private-ROM experiments and is regression-tested across reset and
migration.  Phase 4 remains open because no boot-source selection, image
format, authentication, fallback, TEE entry, reset-domain or hart-release
behavior has been established.

Gate P4:

* reset traces and first instruction match hardware for every boot strap;
* official SPL/U-Boot/OpenSBI binaries reach UART without patched MMIO or DT;
* success, empty media, corrupt image, recovery and fallback paths match;
* cold, warm, watchdog and subsystem reset tests are distinct and repeatable.

### Phase 5 — storage, DMA and Ethernet

Deliver:

* three TH1520 DWC MSHC instances for 8-bit eMMC and SDIO0/1, including PHY,
  tuning, ADMA/CQE and card-detect/write-protect behavior;
* general and secure AXI DMACs with noncoherent memory behavior;
* two GMAC 3.70a instances, APB glue, descriptor DMA, filtering, checksums,
  MDIO and a sufficient RTL8211F model;
* board eMMC, removable microSD and network backends.

Gate P5:

* controller register/reset/IRQ/DMA qtests and error injection pass;
* U-Boot reads/writes each supported boot medium;
* Linux runs filesystem and block-integrity stress on eMMC and microSD;
* DHCP, IPv4/IPv6, TCP/UDP, checksum/offload, multicast and sustained traffic
  pass while stressing noncoherent DMA;
* an official board root filesystem boots unmodified to multi-user.

Status: in progress.  The controller/legacy-card storage submilestone is
implemented and has register, IRQ, PIO, v4 64-bit ADMA2, reset, DT, migration,
and mainline-Linux probe coverage.  An Ahead-only synthetic eMMC 5.1 speed
profile is also implemented with ``EXT_CSD_REV = 8``, ``CARD_TYPE = 0x57``,
``GENERIC_CMD6_TIME = 50`` (500 ms), ``STROBE_SUPPORT = 0`` and
``DRIVER_STRENGTH = 0``/Type 0, validated CMD6 mode changes and standard
four-/eight-bit CMD21 data.  CMD21 is part of the tested reference workflow,
while the card enforces HS plus DDR8 as the immediate HS400 predecessor;
generic eMMC defaults are unchanged.  Three new focused qtests and the
fail-before comparison pass; the complete normal/minimal board gates are
113/112 and the ASan/UBSan storage gate is 13/13.  Pinned Linux reaches HS400
in both builds, but its TH1520 callback intentionally skips CMD21, leaving an
end-to-end CMD21 guest and physical validation open.  A portable single-hart
Linux gate also mounts eMMC as root and preserves one synced 1 MiB payload
across a clean read-only remount and fresh-QEMU-process reopen.  Official
image/U-Boot, SMP, filesystem/block stress and power-loss behavior remain
open.  The initial Ethernet
submilestone integrates both GMAC cores, their APB glue, IRQs, generated DT,
GMAC0's backend and generic Clause 22 PHY;
it now filters rejected frames before touching RX DMA state and covers MAC0
plus 31 additional perfect addresses, address-byte masks and source selection,
promiscuous/receive-all, broadcast/multicast, destination/source inverse,
all four control-frame modes, 64-bin address hashing, C-/S-VLAN exact/VID-only/
inverse matching, and final-descriptor DA/SA/VLAN status.  TH1520 now also
enables Type-2 RX classification/status for bounded IPv4/IPv6, TCP/UDP/ICMP,
one C- or enabled S-VLAN tag, checksum-error and documented bypass paths.
The shared TX engine now models TXCOESEL/TSF gates, first-descriptor CIC0-3,
bounded IPv4/IPv6 TCP/UDP/ICMP insertion, one supported VLAN tag, split-frame
assembly and normal/enhanced status distinctions without changing guest
buffers.  Ten focused GMAC qtests include a deterministic rejection barrier,
a 34-case filter matrix, a Type-2 matrix, an 18-case TX checksum matrix,
split-descriptor boundaries and same-version filter/IPC-state migration with
a separately created destination socket.  The seven-case NPCM suite and the
successful mainline ``dwmac-thead`` probe remain green.  CQE/ADMA3 and
boot/RPMB behavior, SDIO Wi-Fi, card-detect/write-protect wiring and error
injection remain open.  The physical eMMC part, CID/CSD/complete EXT_CSD,
voltage support and electrical/analog HS200/HS400 timing also remain open; the
synthetic speed profile does not resolve them.  The initial general-DMAC
submilestone implements the four-channel controller's direct/linked
memory-copy, descriptor writeback, IRQ/reset/migration and exact DT contracts;
its qtests and all-channel Linux
``dmatest`` pass.  Peripheral handshakes/request routing, dynamic and other
multi-block modes, detailed errors/timing, the secure/TEE DMAC, complete
pause/flow-control behavior, receive checksum drop/threshold, exact transmit
FIFO/PBL/threshold recovery and remaining malformed/fragment/extension/VLAN
corners, PTP/MMC/WOL/EEE and PHY
behavior, block/network stress, stock-image boot, and every remaining P5
acceptance item are still open.  The physical 32-entry/64-bin synthesis and
exact filtering, control-frame and VLAN semantics also require hardware
validation; VLAN-hash mode remains disabled until its presence is established.
P5 is therefore not closed.

### Phase 6 — clocks, reset, power and control I/O

Deliver:

* AP, AO, video, DSP and misc clock/reset controllers, PLL/divider/gate state,
  power domains and safe transition delays;
* six UARTs, six I2C controllers, SPI, QSPI, GPIO banks, pinctrl/padctrl, PWM,
  eight APB timers, RTC, watchdogs, PVT/temperature and ADC;
* mailboxes, spinlocks, system registers, EEPROM, LEDs/buttons, board headers
  and pin-mux conflict behavior;
* deterministic test backends for GPIO, I2C, SPI, ADC and temperature.

Gate P6:

* each in-tree Linux driver binds without ignored errors;
* register, IRQ, clock/reset, power-cycle and migration qtests exist per block;
* header loopback, LED, button, PWM, I2C/SPI peripheral and watchdog tests pass;
* clock-gated/reset devices stop and resume exactly as hardware observations.

Status: in progress.  The first AP clock/reset submilestone implements the two
REE banks, documented digital defaults and masks, seven PLL groups with a
deterministic maximum-default lock delay, reset/migration behavior and the
upstream Linux DT providers.  The UART submilestone integrates all six AP
UARTs at the upstream addresses, PLIC sources and clock IDs, with only UART0
enabled by the board DT.  Its instance test covers DT, routing and reset, and
both migration tests preserve distinct state in all six ports.  The GPIO
submilestone adds a reusable one-port DW APB device and all six TH1520 banks,
including exact bank widths, addresses, PLIC sources, AP clock IDs, DT aliases
and five board LED descriptions.  Three focused tests cover register masks,
pin I/O, edge/level interrupts, reset and migration; pinned Linux binds all six
controllers.  The padctrl submilestone maps all three Linux-described
controllers with exact apertures and clocks, preserves digital PADCFG/MUXCFG
reset words and masks, supplies exact GPIO ranges and board LED, GMAC0, UART0
and Wi-Fi groups, and migrates distinct state.  Two focused tests cover its
register, reset and migration contracts, while the direct-DT test covers its
binding; pinned Linux binds all three padctrl devices.  The I2C submilestone
maps all six controllers with their exact addresses, PLIC sources, AP clock
IDs and synthesis-visible reset identity.  It enables board I2C0 for the
schematic-established 4 KiB EEPROM at address 0x50, supports private raw-image
backing, wraps page writes within the fitted 32-byte page, and migrates
controller/FIFO/EEPROM state.  Five focused tests and a full 4096-byte Linux
AT24 read pass.  The timer submilestone maps both
four-counter blocks and all eight PLIC routes, implements the documented
countdown/status/EOI/reset/migration contract with AP-gated 125 MHz inputs,
and emits eight disabled upstream-compatible DT nodes.  Four named toggle
outputs per component now change at expiry; user-defined PWM mode alternates
``LoadCount`` low intervals and ``LoadCount2`` high intervals, including a
live second-load update.  The AP gate freezes the count and output phase, and
migration preserves the active half-cycle.  Six device qtests, shared
clock-gate tests, a bare-metal access/interrupt payload and the controlled
Linux clocksource probe pass.  The outputs remain deliberately unconnected at
board level.  The watchdog submilestone adds a reusable fixed-TOP DW APB model and
both AP
instances, including exact resource/PLIC/clock/reset DT data, direct and
two-stage interrupt/reset expiry, exact virtual timing, independent AP resets
and migration.  Seven qtests and a pinned-mainline Linux bind/start/ping/stop
probe pass.  The generated nodes remain disabled because mainline has not
established a TH1520 board policy; synthesis identities, the conflicting CCVR
reset value, physical clock-gate behavior and reset scope remain explicit
hardware-validation items.  The RTC submilestone adds a reusable
X-Gene-compatible DesignWare model and maps it at ``0xfffff40000`` with its
32.768 kHz input and PLIC source 74.  It models counter, match, delayed load,
interrupt/mask/EOI, wrap and optional prescaler registers, reset and VMState;
two focused qtests cover register/timing/PLIC and migration behavior.  A
test-only module on the pinned Linux kernel programs the vendor-established
``0x8000`` prescaler, registers ``rtc0``, advances at 1 Hz and receives an
alarm.  The generated node remains disabled because mainline's generic
X-Gene driver lacks this TH1520 sequence.  Component/reset identity, exact
prescaler and wrap edges, calibration, wake and battery/reset retention remain
explicit physical-validation items.  The mailbox submilestone maps the
upstream driver's 24 KiB local resource and its
three remote-ICU resources, including remote ICU0's documented 16 KiB offset,
and emits the exact ``thead,th1520-mbox`` binding with clock IDs 72-75 and PLIC
source 28.  It models the driver's 32-bit status/clear/mask, generate and
INFO0-INFO7 accesses, system reset and migration.  Its remote events are only
an explicit QEMU endpoint hook: no E902, C906, C910R, DSP or AON firmware
protocol is inferred.  Two focused qtests cover the register/PLIC/reset and
migration contracts.  The PVT submilestone maps the four upstream MR75203
resources beginning at ``0xfffff4e000``, with the TH1520 synthesis identity,
73.728 MHz input clock and DT calibration coefficients.  It implements the
driver-used SDIF control and deterministic temperature, process and voltage
sample paths, with QOM-settable environmental inputs, system reset and
VMState.  Two focused qtests cover the DT, register, conversion, counter,
reset and migration contracts, and a pinned Linux build binds it and reads
two temperature and 16 voltage channels.  Alarm comparators, timer behavior,
conversion latency, DONE rearming, interrupt aggregation/routing, exact
rail-to-channel names, analog variation and clock/reset coupling remain open
for hardware comparison.  The SPI0 submilestone maps a reusable DW APB SSI
controller at the exact Linux
address, PLIC source and AP clock ID, and emits the disabled ``spi0`` alias and
compatible strings used upstream.  It supplies a generic synchronous FIFO
master, loopback, error/threshold interrupts and migration without inventing a
board peripheral or flash.  Four focused qtests cover its reset, PLIC, error
and migration contracts.  Its FIFO depth, native chip-select count,
component/probe identity, serial timing, clock-gate behavior, reset-scope validation, pinmux and
physical board wiring remain open.  A 2026-08-24 audit of the pinned mainline
Linux DT and driver tree found QSPI clock/reset identifiers and pad names, but
no QSPI controller node, compatible string, register aperture, IRQ or driver
contract.  QSPI0/1, XIP and boot-media behavior therefore remain outside this
submilestone rather than being inferred from those identifiers.  The TH1520 PWM submilestone maps the six-
channel controller at the exact Linux address and binding, drives test-only
QOM output lines from a provisional 125 MHz clock and preserves staged
configuration/phase across migration.  It does not assert a board pin or
consumer and leaves one-shot, inactive output, aperture, gate and electrical
behavior open.  The AP reset controller now translates all 28
mainline-described groups for modeled AP children into immediate QEMU-device
resets: watchdogs, PWM, timers, UART0-5, I2C0-5, SPI0, GPIO0-3, both AP pad
controllers, DMAC0 and both individual plus shared GMAC domains.  The misc
bank likewise resets eMMC, SDIO0 and SDIO1, in addition to the existing USB
groups.  Raw-output, device-effect, isolation and migration qtests cover this
contract.  Multi-member APB/core/AXI groups are deliberately collapsed to a
whole-device cold reset; accesses remain possible while reset is held, and
GMAC reset currently includes the reusable model's PHY.  These are software
conventions, not claims about physical pulse width, split scope, retention,
ordering or held-reset bus behavior.
The PWM, timer-group and watchdog clock gates stop their timed engines; all
remaining clock gates are still visible state only and do not stop child
models.
Mailbox channel resets and the C910 reset groups remain register-only because
their per-channel retention and hart-release sequencing are not established.
Direct boot also releases all four harts even though the modeled C910
reset-register default releases only the top and core 0.  UART2/4/5 have 16
KiB DT apertures but only the documented first 256-byte DW register block is
mapped; I2C0-5 likewise describe 16 KiB apertures while only the first 4 KiB
is modeled.  Reserved-aperture behavior remains a hardware question.  I2C
slave mode, arbitration/multi-master behavior, timed TX FIFO and bus clock,
clock stretching/stuck recovery, DMA, SMBus, EEPROM write-cycle/write-protect
behavior and factory contents remain open.  GPIO
debounce timing and synthesis probes remain open.  Pad mux changes do not yet
route signals, and physical pulls, voltage domains, drive/slew/Schmitt effects,
tri-state/contention behavior, header conflicts and active-low board-consumer
wiring remain open.  Remaining AP behavior, all other clock/reset and power
domains, SPI0 serial timing/DMA/advanced framing and board peripheral routing,
QSPI/XIP/boot-flash behavior, standalone-PWM one-shot/inactive/output-routing
behavior, timer cascade/0%-100%/initial-edge and physical-output behavior,
PVT alarms/timing/IRQ
and physical calibration, RTC calibration/wake/retention and non-AP
watchdogs, exact
enable/reload/zero-count edges, control I/O and every other P6 gate stay open
until implemented and,
where necessary, compared with the physical board.

### Phase 7 — USB and board radios

Deliver:

* TH1520 USB wrapper, DWC3/xHCI integration, PHY state, host/device/OTG role,
  USB2/USB3 port topology and recovery-mode interaction;
* SDIO wiring and a practical CYW43012/AP6203BM device/backend contract;
* Bluetooth UART/PCM/control wiring and power sequencing.

Gate P7:

* USB enumeration, mass-storage/HID/network stress, hotplug and role-switch
  tests pass;
* Fastboot recovery is compatible with the chosen BootROM mode;
* the same Wi-Fi/Bluetooth firmware and driver initialization succeeds, or a
  precisely documented paravirtual backend boundary is accepted;
* suspend/resume and migration preserve controller-visible state.

RF propagation and real 802.11/Bluetooth air behavior remain backend concerns,
not emulated analog behavior.

Status: in progress.  The first USB-host submilestone maps the misc-system and
DRD wrapper apertures, embeds one DWC3/xHCI host with one USB2 and one USB3 port
for the board's paired connector, routes DMA and PLIC source 68, couples all
three known reset bits, and preserves wrapper/core state across migration.
Register/reset, both ERSTBA half-write orders, command-ring DMA, interrupt,
keyboard hotplug and migration qtests pass.  An upstream Linux 7.2 kernel with
test-only TH1520 glue enumerates the attached keyboard.  The generic DWC3 node
remains generated but disabled until mainline gains a real parent binding and
driver.  Exact DWC3/xHCI synthesis values, distinct reset domains, clocks,
PHY/link behavior, host stress and error paths, suspend/resume, device/OTG,
VBUS/ID role switching, Fastboot/BootROM recovery, Wi-Fi and Bluetooth are
still open, so P7 is not closed.

### Phase 8 — display, GPU, camera and media

Deliver:

* DC8200/DPU scanout, HDMI and DSI pipelines with EDID/hotplug and framebuffer
  formats;
* software-visible PowerVR BXM-4-64 register, MMU, queue, IRQ and firmware
  interaction sufficient for the real driver;
* CSI, ISP, dewarp, G2D/FCE, video decode and encode paths with deterministic
  media backends;
* migration and reset behavior for every stateful engine.

Gate P8:

* unmodified display drivers produce pixel-exact reference frames;
* mode set, hotplug, blanking, suspend and error paths pass;
* GPU/media conformance workloads either match reference output or remain
  explicit blockers—virtio-gpu is not accepted as proof of physical GPU
  emulation;
* camera and codec test streams are frame/checksum-identical where formats
  permit deterministic comparison.

### Phase 9 — NPU and heterogeneous processors

Deliver:

* C906 audio processor, E902 low-power processor and both Vision Q7 DSP
  instances with their local memories, interrupts, timers and firmware loader;
* inter-processor mailboxes, ownership, coherency and wake/power sequencing;
* audio subsystem DMA/I2S/TDM/SPDIF/VAD and deterministic sample backends;
* NPU command, memory, IRQ and firmware-visible execution contract.

Gate P9:

* real auxiliary firmware boots without patching and exchanges messages with
  C910 Linux;
* audio capture/playback is sample-exact for deterministic streams;
* low-power entry/wake paths run through E902 state rather than a board hack;
* documented NPU workloads match reference tensors, including error paths.

Where a proprietary Q7 or NPU ISA cannot legally or technically be implemented,
that is a release blocker, not permission to call the board perfect.

### Phase 10 — security, reliability and lifecycle

Deliver:

* TEE ownership, IOPMPs/firewalls, eFuse/key RAM, secure DMA, secure interrupt
  routing and documented secure-boot behavior;
* all reset domains, brownout/fault injection, watchdog escalation, thermal
  events, clock loss and low-power retention;
* complete VMState, snapshot/replay behavior and versioned compatibility;
* tracing useful for comparing QEMU and hardware without changing guest state.

Gate P10:

* negative access-control and image-authentication tests fail as hardware does;
* reset/power/fault matrix passes from every relevant device state;
* repeated save/load and live migration under CPU, storage, network, USB,
  display, audio and accelerator load is deterministic;
* fuzzers cover all guest-programmable MMIO and descriptor parsers.

### Phase 11 — physical differential validation

Use identical bare-metal probes, firmware, kernels and userspace tests on QEMU
and the physical board.  Capture structured output rather than manually
eyeballing logs.  Compare:

* CSR values, traps, privilege transitions and instruction results;
* reset/default registers, reserved-bit behavior and access widths;
* interrupt numbers, polarity, priority, masking and latency ordering;
* DMA descriptors, coherency requirements, error completion and boundaries;
* boot-source selection and reset/power transitions;
* device protocol results and deterministic output hashes.

Every mismatch receives a ledger ID, minimized reproducer, QEMU trace, hardware
trace, resolution and regression test.  Hardware tests begin read-only and
must not program eFuses or overwrite boot media unless separately authorized.

Gate P11:

* every ledger entry is resolved, explicitly out of software-visible scope, or
  documented as a genuine external blocker;
* the complete supported workload matrix passes repeatedly on both targets;
* no QEMU-only guest patch, DT lie or ignored driver error remains.

### Phase 12 — dependency-proven source pruning

Pruning is authorized for this downstream workspace, including removal of
other target architectures and simplification of dead conditionals.  It is
deliberately last because QEMU shares device, block, networking, TCG, migration,
QAPI, test and utility code across architectures; deleting by directory name
before the board is complete would hide useful models and continuously break
rebases.

Maintain two histories:

1. beaglev-ahead: a full-source, upstream-rebaseable implementation branch.
2. beaglev-ahead-slim: a generated/reviewable downstream branch containing
   only the BeagleV Ahead product.

Pruning procedure:

* configure riscv64-softmmu only, without default devices, using
  beaglev-ahead.mak;
* record the Meson introspection graph, compile_commands.json, linked symbols,
  QOM type list, firmware/data accesses and full test dependency manifest;
* remove all other target architecture directories, user-mode emulators,
  unrelated machine/device source, configs, tests, docs and firmware;
* retain host portability, accel/tcg, RISC-V common CPU code, block/chardev/net,
  QAPI/QOM, migration, crypto, needed device frameworks and build tooling;
* simplify an ifdef only when the remaining branch is proved invariant and the
  replacement still builds under every supported host/compiler configuration;
* run a dangling include/Meson/Kconfig/QAPI/trace/documentation reference
  checker after each deletion batch;
* keep the pruning transformation scripted or as mechanically reviewable
  commits so upstream security fixes remain transplantable.

Gate P12:

* a clean clone of the slim branch configures and builds with the documented
  single command;
* the complete P1–P11 test matrix is identical on full and slim branches;
* no unused target architecture, board type, device model, config symbol,
  source file or conditional branch remains according to the recorded graph;
* license notices and source-offer obligations remain intact;
* rebasing a representative upstream security fix is documented and tested.

## Definition-of-done checklist

The project is complete only when all of the following are proved:

* beaglev-ahead appears in machine help and boots every documented board boot
  mode with unmodified compatible images.
* thead-c910 appears in CPU help and passes scalar, CSR, MMU, PMP, PMU and
  XTheadVector differential suites.
* all four C910 cores, C906, E902, both DSPs and their handoff/power behavior
  meet their phase gates.
* every board-visible device either meets its functional gate or is explicitly
  outside the agreed software-visible definition; no missing block is hidden.
* reset, error, security, migration and fuzz coverage exists, not just boot.
* every uncertainty ledger entry is closed or accepted as an external blocker.
* the BeagleV Ahead-only slim branch passes the same complete suite.
* build, boot, test, hardware-validation and update/rebase instructions are
  reproducible from a clean checkout.

Until then, status reports must name the highest completed gate and the open
gates; “Linux boots” alone must never be described as perfect emulation.
