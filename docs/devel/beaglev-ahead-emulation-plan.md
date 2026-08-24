# BeagleV Ahead / TH1520 QEMU emulation plan

Status: active implementation plan, 2026-08-24

Board: BeagleV Ahead, Seeed/BeagleBoard SKU 102991698

QEMU baseline: bde2492aace2b5acb755a5b057013e915163a77f

Hardware evidence baseline: beagleboard/beaglev-ahead
6b56e2d69485c375c5912eaa2791f79f1d089c07

Linux evidence baseline: 2709dd5ae32f0828f386327c76bba9f39f63a1c6

The companion
[hardware validation ledger](beaglev-ahead-hardware-validation.md) records
every fact that is not yet proved.  An implementation must not silently turn
one of those unknowns into a guessed hardware behavior.

The companion
[upstream bug handoff](beaglev-ahead-upstream-bugs.md) separates defects found
in pre-existing QEMU code from missing emulation, hardware unknowns, and bugs
confined to this not-yet-upstream board implementation.  It records the
reproducer and disclosure work required before any external report.

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
* Firmware and kernel versions used by an official BeagleV Ahead image, pinned
  by commit and image hash before compatibility work starts.

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
| C910 vector | Missing | Implement XTheadVector / RVV 0.7.1 separately from RVV 1.0 |
| T-Head CSRs/MAEE/PMU | C910-specific core CSR state, MAEE PTE acceptance and migration are implemented; PMA timing/cache effects and PMU fidelity remain | Finish CSR probes, memory-attribute effects, exact counters/events and hardware comparison |
| PLIC | A dedicated C900 model now provides 240 sources, eight M/S contexts, five-bit priorities, T-Head delegation, writable pending state, trigger inputs, C900 arbitration, reset and VMState | Confirm TH1520 synthesis parameters, complete trigger/security wiring and boundary behavior on hardware |
| CLINT/timer | A dedicated C900 CLINT now models MSIP/MTIMECMP/SSIP/STIMECMP, 32-bit APB registers, no MMIO mtime, M/S privilege checks, 3 MHz time, reset and VMState | Complete migration, rollover and fault-boundary tests; compare bus-width, latching, reset-domain and clock behavior with the physical TH1520 |
| Clock/reset control | The workspace models the AP clock and reset banks, seven PLL groups, documented reset values/write masks, deterministic PLL locking and VMState; selected documented watchdog, PWM and timer reset lines drive immediate model resets and are replayed after migration; the generated DT uses the upstream Linux providers | Couple gates to children, extend reset coupling beyond watchdog/PWM/timers, model remaining AO/video/DSP/misc domains and power transitions, and validate every default/timing distinction on hardware |
| UART0-5 | This workspace's reusable DW APB wrapper is integrated at all six TH1520 addresses and PLIC sources, with exact upstream clock IDs and board enablement | Verify TH1520 synthesis values, access behavior and the reserved portions of the larger apertures; complete optional shadow/DMA/RS-485 behavior and clock/reset coupling |
| I2C0-5 | The reusable DesignWare model now has configurable synthesis/reset identity, abort/stuck-status registers, reset and validated VMState. All six TH1520 instances have exact Linux addresses, IRQs and clocks; I2C0 carries the 4 KiB board EEPROM and the pinned Linux drivers complete full-image reads | Add timed TX behavior, slave/multi-master/arbitration, clock stretch and stuck recovery, DMA/SMBus, clock/reset coupling, reserved-aperture behavior and physical differential validation |
| USB host | DWC3 host and sysbus xHCI models exist | Add TH1520 wrapper, PHY, OTG/device behavior and exact capabilities |
| SD/eMMC | A reusable DWC MSHC wrapper and all three TH1520 instances now provide SDHCI v4.20, vendor/PHY state, PIO, SDMA, v4 64-bit ADMA2, Auto CMD23, IRQ/reset/migration, eMMC unit 0 and microSD unit 1; mainline Linux probes them with 64-bit ADMA | Add CQE/ADMA3, eMMC 5.1/HS400/boot/RPMB fidelity, SDIO Wi-Fi, removable-card GPIOs, error/tuning injection and mask-ROM storage boot |
| Ethernet | A reusable DWC GMAC 3.x model now provides descriptor DMA, IRQs, FCS, checksum status, Clause 22 MDIO, a configurable PHY and VMState; both TH1520 instances and their APB glue are integrated, and mainline Linux binds GMAC0 as DWMAC1000 | Add programmable MAC/VLAN/hash filtering, full checksum-mode coverage, PTP/MMC/WOL/EEE, RTL8211F vendor pages/delays/IRQ/reset, traffic stress, error injection and physical differential validation |
| SPI/QSPI | A reusable DW APB SSI master is integrated at the Linux-described SPI0 node. The pinned mainline DT/driver tree supplies no QSPI controller node or programming contract, so QSPI/XIP is deliberately not inferred from clock/reset names alone | Validate the TH1520 synthesis and board wiring; add QSPI/XIP only after a public or hardware-established controller/flash contract exists |
| PWM | A six-channel TH1520 PWM controller is integrated at ``0xffec01c000`` with its Linux binding, AP clock ID 51, aligned 32-bit control/period/falling-point registers, continuous normal/inverted waveforms, boundary-latched reconfiguration, reset and VMState.  It uses a provisional fixed 125 MHz QEMU input, exposes test-only QOM outputs, and resets immediately when either known AP PWM reset bit is asserted; the board has no generated PWM consumer | Validate reset/register/strobe semantics, clock rate/gating, one-shot/inactive behavior, the rest of the 16 KiB aperture, pinmux/header routing and safe physical electrical behavior |
| GPIO/pinctrl | A reusable one-port DW APB GPIO model and all six Linux-described TH1520 banks now provide 157 lines, exact IRQ/clock/DT wiring, edge/level interrupts, reset and VMState.  All three TH1520 pad controllers provide software-visible PADCFG/MUXCFG state, exact apertures/clocks, digital reset values/write masks and VMState; the board DT includes exact GPIO ranges and LED/GMAC0/UART0/Wi-Fi groups | Validate GPIO synthesis IDs, direction wording and debounce timing plus pad resets and electrical effects on hardware; add GPIO consumer wiring, mux-driven signal routing and deterministic header/device backends |
| APB timers | A reusable four-counter DesignWare model and both TH1520 components now provide eight 125 MHz countdown channels, PLIC sources 16-23, local/aggregate EOI and status, reset and VMState; either known APB/core reset bit immediately resets its corresponding component; all eight upstream-DT nodes remain board-disabled | Validate component synthesis, clocks, access widths, reload/zero/enable edges, cascade/PWM and reset-domain behavior on hardware; couple clock gates and remaining resets |
| PVT/thermal/voltage | A reusable MR75203 model maps the exact TH1520 common, temperature, process and voltage apertures, synthesis identity, 2 temperature sensors, 11 process detectors and 16 voltage channels.  It implements the Linux SDIF programming path, deterministic QOM environment inputs, reset and VMState; pinned Linux binds and reads all advertised temperature and voltage channels | Validate physical samples and calibration across temperature/voltage, conversion latency and DONE behavior, sample-counter edges, alarm/timer/register semantics, any interrupt route, access widths, clock/reset coupling and actual rail-to-channel names on the owner board |
| RTC/watchdog | A reusable fixed-TOP Synopsys DW APB watchdog model and both TH1520 AP instances now provide exact countdown/restart, direct and two-stage interrupt/reset behavior, PLIC sources 24/25, independent AP resets, VMState and conservative disabled DT nodes.  Pinned Linux binds, starts, pings and reset-stops both through an external enabling DT.  RTC and AO/audio watchdogs remain absent | Validate watchdog identities, clock/reset scope and edge behavior on hardware; couple the AP clock gates; implement RTC only after resolving the vendor-only 32.768 kHz prescaler contract; add remaining watchdog domains from public or measured evidence |
| AXI DMAC | A reusable DW AXI DMAC 1.01a model now provides four-channel direct and linked-list memory-to-memory DMA, descriptor writeback, error/IRQ state, reset and VMState; the TH1520 general instance has exact mainline-DT wiring and the Linux driver plus `dmatest` exercise all channels | Add peripheral request/handshake wiring, secure/TEE instance, contiguous/reload/shadow/cyclic and dynamic-LLI modes, detailed fault/suspend/timing behavior, noncoherent cache effects and physical differential validation |
| Mailbox/system control | A bounded TH1520 mailbox model maps the four upstream-Linux resources, CPU-visible channel data/generate registers, local status/clear/mask, PLIC source 28, system reset and VMState; it deliberately has no remote CPU or firmware response | Validate the register/pulse/reset/gate behavior and add E902/C906/C910R/DSP endpoints plus their documented handoff/control protocols |
| GPU/DPU/HDMI/DSI | Matching models missing | New software-visible register/queue/display pipelines |
| NPU/camera/codec/ISP | Missing | New functional command/data-path models |
| C906/E902/DSPs | C906 CPU model is partial; E902/Q7 system integration missing | Add exact cores or execution adapters, memories, IRQs and firmware handoff |
| Security/IOPMP/eFuse | Missing | New access-control, fuse/key, TEE and secure-boot state |
| Migration | Current C910, CLINT, PLIC, AP clock/reset, UART, I2C and board EEPROM, SPI0, TH1520 PWM, APB timer, both AP watchdogs, TH1520 mailbox, MR75203 PVT, GPIO, TH1520 padctrl, DWC MSHC, DWC GMAC, TH1520 GMAC APB glue, DW AXI DMAC, DRAM and SRAM state has VMState and focused regression coverage; established boot-critical state also has a whole-machine regression | Extend the same state inventory and boundary testing to every new controller and backend; add in-flight state if the synchronous DMAC model later gains timing |

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
  TH1520 no-PMP configuration, the initial custom CSR bank, migration state,
  and provisional MAEE PTE acceptance;
* the C9xx PMU's 16 programmable counters, raw-selector WARL rules,
  machine/supervisor overflow CSRs, delegable local cause 17, exact Linux DT
  event maps, and focused CSR/fixed-counter overflow tests; microarchitectural
  event values remain an explicit hardware-differential task;
* XTheadVector decode/translation/helpers, 128-bit vector state, T-Head status
  and CSR behavior, debugger/migration integration, and focused qtest/TCG
  smoke coverage; and
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
  persistent writes.  EEPROM memory and its current-address state migrate;
  five focused tests cover DT/reset/identity, all PLIC routes, repeated-start
  data access, backing persistence and in-flight migration.  A pinned Linux
  build with AT24 enabled binds both drivers and reads all 4096 bytes; and
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
  implemented with a provisional 125 MHz input.  Three focused qtests cover
  all six channel register banks, polarity/timing and pending-update
  migration; the direct-DT test covers the binding.  The model exposes six
  QOM output lines only for emulator tests: the documented AP reset pair
  immediately restores its digital reset state, but no physical pin, board
  consumer, one-shot/inactive-output behavior, AP gate coupling or reserved-
  aperture behavior is claimed; and
* a reusable four-counter DesignWare APB timer with aligned 32-bit load,
  current, control, EOI, local and aggregate status, component-version,
  second-load and protection registers.  Periodic and free-running countdown,
  masking, raw interrupt latching, reset and VMState are functional.  Two
  TH1520 components map timers 0-7 at ``0xffefc32000``/``0xffffc33000`` with a
  fixed 125 MHz clock and PLIC sources 16-23.  Either documented APB/core
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
  ``0xffefc30000``/``0xffefc31000`` with a provisional 125 MHz clock, PLIC
  sources 24/25 and independent active-low AP resets.  Seven qtests cover
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
  five board LEDs on GPIO4 pins 8-12.  Focused qtests cover every bank, pin
  I/O, both interrupt modes and pending-edge migration; the pinned Linux
  driver binds all six controllers; and
* a TH1520 pad-controller model with the three always-on and application-domain
  instances at their exact apertures and clocks.  It preserves the documented
  digital PADCFG/MUXCFG reset words, reserved-bit masks, system reset and
  VMState.  The generated DT reproduces all six Linux GPIO range mappings and
  the board's LED, GMAC0, UART0 and Wi-Fi pin groups; pinned Linux binds all
  three controllers.  Actual mux-driven signal routing, pad electrical effects
  and PHY/Wi-Fi/card-detect consumer wiring remain open hardware-validation
  work; and
* a reusable DesignWare Mobile Storage Host Controller wrapper with the
  TH1520's 64 KiB aperture, vendor pointers, v4.20 capabilities, vendor and
  PHY register state, deterministic power-good/DLL-lock behavior, VMState,
  and all three eMMC/SDIO instances at their physical addresses and PLIC
  sources.  Generic SDHCI now accepts v4 controllers, preserves Host Control 2,
  implements Auto CMD23 and 128-bit 64-address ADMA2 descriptors, and restores
  its interrupt output after migration; and
* a reusable DesignWare GMAC 3.x model, factored from the NPCM implementation,
  with normal and enhanced descriptors, 16/32-byte descriptor stride, TX/RX
  DMA, FCS handling, bus errors, interrupt recomputation, configurable version/
  feature/PHY identity and VMState.  Both TH1520 GMACs are mapped at their
  physical core/APB addresses and PLIC sources, with the nine-register APB
  reset/mask contract, separate 500 MHz AXI, 1 GHz peripheral and 125 MHz APB
  clocks from the AP clock provider, board GMAC0/RTL8211F-facing DT wiring,
  and disabled board GMAC1; and
* a reusable Synopsys DesignWare AXI DMAC 1.01a model with four channels,
  direct and 64-byte-LLI memory-to-memory transfers, 64-bit addresses,
  transfer-width and increment behavior, descriptor valid/last/writeback,
  block/transfer/error status, interrupt masking and aggregation, reset and
  VMState.  The TH1520 general controller is mapped at ``0xffefc00000`` on
  PLIC source 27 with the exact four-channel Linux binding and a measured
  125 MHz APB clock-provider contract; and
* TH1520 AP clock and reset controllers at ``0xffef010000`` and
  ``0xffef014000`` with the documented REE register banks, seven PLL groups,
  reset values and writable masks, deterministic 21.25 microsecond PLL-lock
  delay, self-clearing calibration pulses, system reset and VMState.  The
  generated DT now exposes the upstream Linux bindings and uses their real
  clock IDs for all six UARTs, the general DMAC, all three storage controllers
  and both GMACs instead of temporary fixed-clock nodes; and
* a deterministic direct-boot contract that selects hart 0 for both the
  FW_DYNAMIC relocation stage and OpenSBI's later cold-boot lottery, plus a
  four-hart M-mode payload whose ordered UART transcript proves that harts
  0 through 3 all entered the common reset path.  A supplied ``-dtb`` now
  replaces the generated tree rather than being silently ignored, with a
  marker round-trip qtest; and
* a whole-machine migration test that moves DRAM, SRAM, per-hart base and
  C910-specific CSR state, the rotating CPUID cursor, architectural time,
  CLINT, PLIC, AP clock/reset state, distinct state in all six UARTs and all
  six I2C and GPIO controllers, the board EEPROM, SPI0, both APB timer
  components, TH1520 mailbox state, all three pad controllers, all three
  storage controllers, both GMAC cores, PHY banks and both GMAC APB-glue
  instances together; and
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

The focused gate currently comprises 82 board qtests in the normal,
dependency-minimal and ASan/UBSan builds.  These include eight storage tests
for the generated DT, exact controller/PHY reset and masks, all three PLIC
routes, configurable unknown synthesis IDs, eMMC PIO read/write, SD Auto CMD23
with a 64-bit ADMA descriptor and buffer above 4 GiB, and device migration.
Four GMAC tests cover the exact DT/clock/APB/MDIO contract, masked APB writes,
both PLIC routes, enhanced 32-byte TX/RX descriptors, FCS, extension-word
preservation, and a socket-backed packet path.  Four DMAC tests cover
reset/masks, a direct copy and PLIC route, a two-item LLI chain above 4 GiB,
invalid-descriptor failure and completed-state/IRQ migration; the direct-boot
test also checks the complete generated binding and AP clock-provider IDs.
Three AP clock/reset tests cover reset values, writable masks, PLL restart and
lock delay, calibration self-clear, system reset, reset-bank behavior and
migration while a PLL lock is pending.  A six-instance UART test verifies the
exact addresses, aperture descriptions, clock IDs, serial aliases, board
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

With a blank 64 MiB image attached as storage unit 0, the same pinned Linux
kernel binds all three ``thead,th1520-dwcmshc`` nodes, reports 64-bit ADMA, and
enumerates the image as a high-speed ``QEMU!!`` eMMC block device.  This proves
the mainline driver/controller contract through block discovery, not eMMC 5.1,
HS400, filesystem integrity, CQE, SDIO Wi-Fi, physical-card GPIOs, or mask-ROM
boot behavior.

The same pinned Linux source was also rebuilt in a separate output directory
with ``CONFIG_STMMAC_ETH``, ``CONFIG_STMMAC_PLATFORM`` and
``CONFIG_DWMAC_THEAD`` built in.  It binds GMAC0 at ``0xffe7070000`` without a
probe error or clock-divider warning, reads user/version ID ``0x10/0x37``, and
selects DWMAC1000, RGMII, Type-2 RX checksum, TX checksum insertion,
enhanced/extended descriptors and ring mode.  This establishes the mainline
driver/register contract only.  It does not establish a working physical link,
full packet-offload correctness, the provisional hardware-feature aggregate,
RTL8211F vendor behavior, or any of the traffic/stress requirements in P5.

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

Status: in progress.  The legacy storage submilestone is implemented and has
register, IRQ, PIO, v4 64-bit ADMA2, reset, DT, migration, and mainline-Linux
probe coverage.  The initial Ethernet submilestone integrates both GMAC cores,
their APB glue, IRQs, generated DT, GMAC0's backend and generic Clause 22 PHY;
it has reset/mask/MDIO/IRQ/enhanced-descriptor/socket/migration qtests and a
successful mainline ``dwmac-thead`` probe.  CQE/ADMA3, eMMC 5.1/HS400 and
boot/RPMB behavior, SDIO Wi-Fi, card-detect/write-protect wiring and error
injection remain open.  The initial general-DMAC submilestone implements the
four-channel controller's direct/linked memory-copy, descriptor writeback,
IRQ/reset/migration and exact DT contracts; its qtests and all-channel Linux
``dmatest`` pass.  Peripheral handshakes/request routing, dynamic and other
multi-block modes, detailed errors/timing, the secure/TEE DMAC, complete
filtering/PTP/MMC/WOL/EEE and PHY behavior, block/network stress, stock-image
boot, and every remaining P5 acceptance item are still open.  P5 is therefore
not closed.

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
backing, and migrates controller/FIFO/EEPROM state.  Five focused tests and a
full 4096-byte Linux AT24 read pass.  The timer submilestone maps both
four-counter blocks and all eight PLIC routes, implements the documented
countdown/status/EOI/reset/migration contract at a fixed 125 MHz, and emits
eight disabled upstream-compatible DT nodes.  Four qtests, a bare-metal
access/interrupt payload and the controlled Linux clocksource probe pass.  The
watchdog submilestone adds a reusable fixed-TOP DW APB model and both AP
instances, including exact resource/PLIC/clock/reset DT data, direct and
two-stage interrupt/reset expiry, exact virtual timing, independent AP resets
and migration.  Seven qtests and a pinned-mainline Linux bind/start/ping/stop
probe pass.  The generated nodes remain disabled because mainline has not
established a TH1520 board policy; synthesis identities, the conflicting CCVR
reset value, clock-gate coupling and physical reset scope remain explicit
hardware-validation items.  A parallel RTC audit found only a vendor
``apm,xgene-rtc`` node whose local driver adds a 32.768 kHz prescaler contract
absent from mainline, so RTC modeling is deferred instead of guessing.  The
mailbox submilestone maps the upstream driver's 24 KiB local resource and its
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
component/probe identity, serial timing, gate/reset behavior, pinmux and
physical board wiring remain open.  A 2026-08-24 audit of the pinned mainline
Linux DT and driver tree found QSPI clock/reset identifiers and pad names, but
no QSPI controller node, compatible string, register aperture, IRQ or driver
contract.  QSPI0/1, XIP and boot-media behavior therefore remain outside this
submilestone rather than being inferred from those identifiers.  The TH1520 PWM submilestone maps the six-
channel controller at the exact Linux address and binding, drives test-only
QOM output lines from a provisional 125 MHz clock and preserves staged
configuration/phase across migration.  It does not assert a board pin or
consumer and leaves one-shot, inactive output, aperture, gate and electrical
behavior open.  The AP reset controller now translates the two documented
watchdog lines, PWM pair and two timer APB/core reset pairs into immediate
QEMU-device resets; the derived outputs are re-emitted after migration.  This
is a software model, not a claim about pulse width, retention, ordering or bus
behavior while held.
Clock gates and all remaining reset outputs are not yet coupled to UART, I2C,
SPI0, GPIO, DMA, storage, GMAC or the harts, so their guest gate/reset writes
currently change controller state without stopping those child models.
Direct boot also releases all four harts even though the modeled C910
reset-register default releases only the top and core 0.  UART2/4/5 have 16
KiB DT apertures but only the documented first 256-byte DW register block is
mapped; I2C0-5 likewise describe 16 KiB apertures while only the first 4 KiB
is modeled.  Reserved-aperture behavior remains a hardware question.  I2C
slave mode, arbitration/multi-master behavior, timed TX FIFO and bus clock,
clock stretching/stuck recovery, DMA, SMBus, EEPROM page-wrap/write-cycle/
write-protect behavior and factory contents remain open.  GPIO
debounce timing and synthesis probes remain open.  Pad mux changes do not yet
route signals, and physical pulls, voltage domains, drive/slew/Schmitt effects,
tri-state/contention behavior, header conflicts and active-low board-consumer
wiring remain open.  Remaining AP behavior, all other clock/reset and power
domains, SPI0 serial timing/DMA/advanced framing and board peripheral routing,
QSPI/XIP/boot-flash behavior, standalone-PWM one-shot/inactive/output-routing
behavior, timer cascade/load-count-2 waveform behavior, PVT alarms/timing/IRQ
and physical calibration, RTC and non-AP watchdogs, exact
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
