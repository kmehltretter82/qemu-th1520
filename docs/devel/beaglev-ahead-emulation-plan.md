# BeagleV Ahead / TH1520 QEMU emulation plan

Status: active implementation plan, 2026-08-23

Board: BeagleV Ahead, Seeed/BeagleBoard SKU 102991698

QEMU baseline: eea8fe61b8be8f3016e522e6af24924a0266ca95

Hardware evidence baseline: beagleboard/beaglev-ahead
6b56e2d69485c375c5912eaa2791f79f1d089c07

Linux evidence baseline: a2ff812c7712fc5021030e528f42494ca4a1e185

The companion
[hardware validation ledger](beaglev-ahead-hardware-validation.md) records
every fact that is not yet proved.  An implementation must not silently turn
one of those unknowns into a guessed hardware behavior.

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
| PLIC | Configurable SiFive PLIC exists | Verify or derive a C900/TH1520 wrapper, 240 sources and eight contexts |
| CLINT/timer | A dedicated C900 CLINT now models MSIP/MTIMECMP/SSIP/STIMECMP, 32-bit APB registers, no MMIO mtime, M/S privilege checks, 3 MHz time, reset and VMState | Complete migration, rollover and fault-boundary tests; compare bus-width, latching, reset-domain and clock behavior with the physical TH1520 |
| UART0-5 | Generic 16550 serial-mm exists | Add DW APB wrapper/probe registers, reg-shift 2, 32-bit accesses and clocks |
| I2C0-5 | DesignWare I2C model exists | Add TH1520 integration, parameters, DMA/IRQ/reset behavior |
| USB host | DWC3 host and sysbus xHCI models exist | Add TH1520 wrapper, PHY, OTG/device behavior and exact capabilities |
| SD/eMMC | SD card and generic SDHCI foundations exist | New DWC MSHC controller/glue, PHY, tuning, CQE and three instances |
| Ethernet | No Synopsys DWMAC/stmmac model found | New GMAC 3.70a, MDIO and RTL8211F-facing behavior |
| SPI/QSPI | Generic SSI/flash infrastructure exists | New DW APB SSI and TH1520 QSPI/XIP integration |
| GPIO/pinctrl/PWM | No matching DW APB GPIO or TH1520 pinctrl/PWM | New reusable IP and TH1520 glue |
| APB timers/RTC/watchdog | Timer framework exists; no matching TH1520 set | New register models and clock/reset behavior |
| AXI DMAC | No matching Synopsys AXI DMAC model | New descriptor engine, channels, IRQs and noncoherent DMA behavior |
| Mailbox/system control | Missing | New C910/C906/E902/DSP handoff and control-plane models |
| GPU/DPU/HDMI/DSI | Matching models missing | New software-visible register/queue/display pipelines |
| NPU/camera/codec/ISP | Missing | New functional command/data-path models |
| C906/E902/DSPs | C906 CPU model is partial; E902/Q7 system integration missing | Add exact cores or execution adapters, memories, IRQs and firmware handoff |
| Security/IOPMP/eFuse | Missing | New access-control, fuse/key, TEE and secure-boot state |
| Migration | Not applicable until devices exist | VMState for every persistent/volatile modeled state item |

## Workspace implementation status

The branch is being advanced in reviewable milestones rather than treating
the roadmap as a claim of completion.  At the current milestone it contains:

* a dependency-minimal ``beaglev-ahead`` machine with four ``thead-c910``
  harts, the physical RAM/SRAM/ROM map, PLIC, CLINT, UART0, generated DT, and
  direct OpenSBI/kernel boot;
* C910 scalar identity, 40-bit physical-address constraints, the TH1520
  no-PMP configuration, the initial custom CSR bank, migration state, and
  provisional MAEE PTE acceptance;
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
  delivery test; and
* a minimal device build that excludes unrelated boards and most unused
  devices without deleting shared source prematurely.

The CLINT portion of the Phase 1 interrupt gate is implemented, but Phase 1 is
not closed: the C900 PLIC, UART, SMP payload, Linux, sanitizer, and migration
gates remain.  Phases 2 and 3 likewise retain their exhaustive and physical-
differential gates.  All provisional behavior is linked to an open item in the
companion ledger.

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
