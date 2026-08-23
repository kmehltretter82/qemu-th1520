# BeagleV Ahead hardware validation and uncertainty ledger

Status: hardware not yet powered; all physical observations are pending

Board identity supplied by owner:

* distributor part: 2820-102991698-ND
* manufacturer: BeagleBoard by Seeed Studio
* manufacturer part: 102991698
* description: BeagleV Ahead

This file is part of the implementation contract.  Anything listed as open
must not be presented as known hardware behavior in code comments, commit
messages, documentation, or tests.

## Ledger rules

Use these states:

* OPEN-DOC: public sources disagree, omit a required detail, or are ambiguous.
* OPEN-HW: a controlled observation on the owner's board is required.
* OPEN-LEGAL: implementation provenance or redistribution needs a decision.
* IN-PROGRESS: a reproducer is being run or evidence is being analyzed.
* RESOLVED: evidence, result, implementation consequence and regression test
  are all linked in this file.
* OUT-OF-SCOPE: explicitly accepted as outside software-visible QEMU fidelity,
  with the reason and substitute/backend behavior recorded.

Resolution requires:

1. immutable evidence: repository commit and path, document revision/page, or
   hardware capture hash;
2. the observed value and conditions;
3. the QEMU behavior selected from that evidence;
4. a test that would fail if the behavior regresses.

Never close an item merely because Linux boots or a driver does not complain.

## Safe hardware workflow

The first hardware session is read-only and non-destructive:

1. Photograph both PCB faces, stickers, connector population and readable chip
   markings.  Record board revision and TH1520 marking.
2. Use a current-limited known-good supply and capture power-up UART from before
   reset release.  Do not attach expansion hardware initially.
3. Preserve the factory storage: record GPT/MBR, partition UUIDs, hashes,
   bootloader versions, DTBs, kernel config and firmware inventory before any
   update.
4. Boot a signed or owner-approved probe from removable media.  Read only
   documented side-effect-free CSRs and MMIO registers.
5. Add one controlled subsystem test at a time and preserve raw serial/logic
   analyzer/network/USB captures.
6. Do not write eFuses, key RAM, OTP, calibration storage, SPI flash or factory
   eMMC boot areas.  Do not enable undocumented clocks or power rails.

Potentially destructive boot-media writes, voltage changes, shorts/loopbacks,
thermal tests, security tests and fault injection require a separate explicit
authorization and a recovery procedure.

## Capture format

Store future captures outside the QEMU source tree under a date-stamped test
run directory, with a small tracked manifest containing:

* ledger ID and test program commit;
* PCB revision, SoC marking and board serial alias;
* boot image and every binary/DTB hash;
* power supply, UART settings and attached hardware;
* exact steps and expected safe side effects;
* raw capture filenames and SHA-256 hashes;
* parsed result, QEMU comparison and pass/fail;
* follow-up issue/test/commit.

Do not commit secrets, unique keys, MAC addresses, Wi-Fi credentials, board
serials, proprietary firmware or copyrighted manuals.  Redact public reports
while retaining a private evidence hash.

## Open ledger

| ID | State | Question or conflict | Current evidence / implementation rule | Planned resolution |
| --- | --- | --- | --- | --- |
| ID-001 | OPEN-HW | Which PCB assembly/revision is SKU 102991698 in hand? | Official design filenames contain A11, but that does not prove the shipped revision. Do not bake a revision assumption into the machine. | Photograph PCB revision, stickers and population; compare schematic/BOM variants. Add a board-revision property only if behavior differs. |
| ID-002 | OPEN-HW | Exact TH1520 silicon stepping and package marking? | BOM names TH1520-C0-B00001 while an older design BOM contains a less specific C0X entry. | Record U1 marking and read safe chip-ID/system-register fields. |
| DOC-001 | OPEN-LEGAL | May implementations derived from publicly hosted, “Secret”-marked TH1520 manuals be accepted upstream? | The official BeagleBoard repository publishes the manuals, but their page markings and copyright text create provenance uncertainty. No PDF or substantial quotation belongs in this tree. | Record download URL/commit/hash, consult QEMU maintainers or counsel, and prefer public Linux drivers/specifications plus black-box tests for upstream patches. |
| DOC-002 | OPEN-DOC | Which vendor firmware/kernel/U-Boot sources exactly correspond to current official images? | Public documentation links multiple images and evolving sources. | Pin an official image; inventory strings/build IDs; map every binary to source commit or record it as opaque. |
| DOC-003 | OPEN-LEGAL | Is the XTheadVector port's provenance and attribution acceptable for upstream submission? | The implementation was ported from the GPL-2.0-or-later Alibaba/XuanTie QEMU fork at 3287d345c7f5d60d5c8774d90752f5f710744f85 and reconciled with upstream's older RVV 0.7.1 code at e523773040ed914b60c8b68c25a96c88b2bb112a. File copyright and license notices are retained. | Preserve a mechanical-port audit trail, run QEMU's provenance process, disclose the source commits in cover letters, and obtain maintainer confirmation before upstream submission. |
| CPU-001 | OPEN-HW | Exact C910 mvendorid, marchid, mimpid and custom CPU ID values? | Linux errata code supports the T-Head vendor ID with zero marchid/mimpid for C9xx. The provisional seven-value CPUID sequence comes from the TH1520 material; it is not assumed to cover every stepping. | M-mode CSR probe on each hart; repeat after warm reset and compare the complete rotating CPUID sequence. |
| CPU-002 | OPEN-DOC | Exact C910 privileged ISA revision and all implemented standard extensions? | Linux DT advertises RV64IMAFDC plus ziccrse/zicntr/zicsr/zifencei/zihpm/zfh/xtheadvector; manuals and firmware may expose more. | Decode firmware ISA strings, audit public C910 manuals/spec, then probe misa and behavior rather than trusting misa alone. |
| CPU-003 | OPEN-HW | Custom CSR reset values, writable masks, privilege checks and inter-hart scope? | A C910-specific bank now models MXSTATUS/SXSTATUS, MHCR/SHCR, MCOR, MHINT1-3, MRVBR, MCNTWEN and CPUID using official openC910 RTL commit b91c90914c19f114d35c8f6b73408eb241ed847c plus TH1520 integration data. Other custom CSRs remain explicit zero-valued placeholders. | Generated M/S/U CSR probe with read/write/read, illegal-access trap capture and warm/cold reset, restricted to documented safe fields. |
| CPU-004 | OPEN-HW | MAEE page-table encoding and memory-type semantics? | QEMU now resets MAEE enabled, permits C910 PTE bits 63:59, masks PPN/SATP to 40 physical bits and flushes translations when MAEE changes. Cacheability, ordering, shareability and security effects are not yet represented. | Bare-metal mappings over RAM and a harmless MMIO scratch target; test access, ordering, aliases and faults with/without MAEE. |
| CPU-005 | OPEN-HW | XTheadVector availability/version and feature identification on C910? | Board sources say Vector 0.7.1 and Linux uses xtheadvector with VLENB 16. QEMU now implements the frozen XTheadVector 1.0 specification at e744688edd2f88be2e032c67e20789030436ac08, advertises VLENB 16, reports the identifying misa.V bit, and rejects RVV 1.0-only vsetivli. The specification's availability text does not clearly name C910. | Read vector CSRs and run the same discriminating 0.7.1-versus-1.0 encodings on all harts, capturing illegal traps and results. |
| CPU-006 | OPEN-HW | Exact vector tail, mask, vstart, saturation, FP flags and fault-only-first edge behavior? | QEMU has a broad vendor-derived XTheadVector execution engine, but current testing is a scalar/vector load-add-store and CSR/illegal-encoding smoke test. The extension spec gives architectural rules, while silicon errata/revision behavior remains unknown. | Build one boundary test per instruction form, then a randomized differential suite with guarded pages and signal/trap recovery; keep per-stepping expected results. |
| CPU-007 | OPEN-HW | PMP entry count, physical-address width, grain, reset, locking and interaction with TEE/security extensions? | System documentation says 32 entries and a 40-bit physical address space. QEMU currently limits C910 pmpaddr to the corresponding 38 stored bits, but the board model still exposes 16 entries pending a safe probe. | Safe M-mode PMP probe in disposable address ranges, including implemented-bit discovery and TOR/NA4/NAPOT; test lock only where reset recovery is certain. |
| CPU-008 | OPEN-HW | PMU event numbers, counter width, overflow IRQ and inhibit/delegation behavior? | Mainline DT maps standard and raw events, but does not prove hardware counts or privilege filters. | Deterministic instruction/cache/branch workloads and overflow tests, normalized rather than cycle-count compared. |
| CPU-009 | OPEN-HW | Architecturally visible cache/CMO, noncoherent DMA and ordering behavior? | QEMU's current XTheadCmo operations are largely no-ops because QEMU does not model caches. TH1520 DT declares dma-noncoherent. | DMA buffer tests with/without each clean/invalidate/sync sequence and multi-hart litmus tests. |
| CPU-010 | OPEN-HW | Misaligned load/store/atomic/vector behavior and exception priority? | Generic QEMU behavior must not be assumed to match C910. | Guarded-page bare-metal matrix for widths, privilege levels and scalar/vector/atomic classes. |
| CPU-011 | OPEN-HW | Debug/security extensions and reset visibility needed by guest software? | System documentation mentions debug and interrupt/L2 security extensions without a complete public software contract. The pinned Linux DT does not advertise standardized Sdtrig, which postdates the modeled Privileged ISA 1.10, so the board machine defaults QEMU's generic debug/Sdtrig feature off rather than emitting it in the generated DT. | Inventory OpenOCD/vendor debug behavior; probe only guest-visible registers and traps. If C910 implements a legacy trigger contract, model and name that contract without falsely advertising Sdtrig. |
| CPU-012 | OPEN-HW | Do th.vxrm and th.vxsat require floating-point state to be enabled, and are they absent from fcsr? | QEMU follows the mainline Linux C9xx handling: vector fixed-point CSR access requires FS rather than VS alone, and fcsr does not alias the fields. This is an implementation rule, not a physical observation. | Probe th.vxrm, th.vxsat and fcsr for every FS/VS state in M/S/U modes, capturing read/write results and illegal traps. |
| CPU-013 | OPEN-HW | Does the TH1520 C910 implement the separately named XTheadZvamo extension? | The frozen XTheadVector specification explicitly renames Zvamo to XTheadZvamo, but available board descriptions do not advertise it. QEMU keeps the separately selectable implementation disabled on C910 and tests that a valid vector-AMO encoding traps. | Run aligned, mapped vector-AMO probes at valid SEW/LMUL on all harts; first establish a reset/recovery path and use disposable RAM because a supported instruction mutates memory. |
| MEM-001 | OPEN-DOC | Exact 4 GiB DRAM usable map, aliases, reserved/secure carveouts and top-of-RAM behavior? | Board DT declares 0x0–0xffffffff, while the SoC exposes a larger DDR aperture and firmware may reserve regions. | Parse stock DT/reserved-memory, boot logs and page tables; safe RAM boundary test from removable boot. |
| MEM-002 | OPEN-HW | On-chip SRAM/retention RAM size, reset contents, aliases and retention across reset classes? | Manual address map gives regions, but board/silicon behavior and ECC initialization need observation. | Pattern test only in firmware-declared free regions across subsystem, warm and cold reset. |
| BOOT-001 | OPEN-HW | Exact reset PC, initial hart states, Core0 TEE mode and secondary-hart release sequence? | Manual says C910 starts first and Core0 defaults to TEE; C906/E902 are woken later. | Capture reset with JTAG if safe and instrument earliest boot code; trace mailbox/reset-vector writes. |
| BOOT-002 | OPEN-HW | Exact BOOT_SEL straps on this PCB and accessible user controls? | Manual defines USB/eMMC/SD/QSPI choices; schematic-to-board routing has not been validated. | Photograph/schematic trace, read strap status register, test reversible supported selections without writing media. |
| BOOT-003 | OPEN-DOC | BootROM image format, search/fallback policy, authentication and error behavior? | High-level boot modes are documented; exact ROM algorithm and redistribution rights are not. | Analyze legal public boot tools/source and UART/USB/media traces using valid, empty and deliberately malformed removable media. |
| BOOT-004 | OPEN-HW | UART CCT and USB Fastboot handshake details and timeouts? | Manual names both recovery paths but is insufficient for an exact state machine. | USB protocol capture and UART logic capture with vendor tools, using non-writing identification commands first. |
| BOOT-005 | OPEN-HW | Cold, warm, watchdog, software and individual-domain reset differences? | Reset register documentation is not proof of retained state or sequencing. | Reset matrix recording PC, CSR/MMIO defaults, SRAM retention, clocks and peripheral state. |
| INTC-001 | OPEN-HW | Does the TH1520/C900 PLIC differ from QEMU's SiFive PLIC in layout, priority bits, pending/claim semantics or contexts? | Linux DT supplies 240 sources, base 0xffd8000000 and eight M/S contexts. Compatible string is not sifive,plic. | Register probe plus controlled UART/timer/GPIO interrupts on every context, priority and threshold. |
| INTC-002 | OPEN-HW | Exact CLINT offsets, access widths, timer rollover/latching and 3 MHz stability? | Linux DT maps 64 KiB at 0xffdc000000 and gives timebase 3 MHz. | Compare safe reads/writes, rollover and per-hart SWI/timer IRQs against an external time reference. |
| IRQ-001 | OPEN-DOC | Complete interrupt-number/polarity/trigger map for blocks absent from mainline DT? | Mainline DT covers only a subset of the complete SoC and may encode binding offsets. | Build a machine-readable map from manuals/vendor DT; validate each source by causing exactly one controlled interrupt. |
| CLK-001 | OPEN-HW | Oscillator/PLL/divider reset rates and readable clock-status values? | Board DT gives 24 MHz and 32.768 kHz inputs; product pages quote up to 1.85 GHz but not every reset rate. | Read clock tree after cold reset and after stock firmware configuration; measure UART/timer-derived rates. |
| CLK-002 | OPEN-HW | Clock gate, PLL lock delay, DVFS and invalid-transition behavior? | Driver-visible register definitions do not establish timing or fault semantics. | Trace stock cpufreq and controlled gate/reset tests; use order relationships, not host wall-clock performance, in QEMU. |
| PWR-001 | OPEN-HW | AP/AO/video/GPU/DSP power-domain retention and wake ordering? | E902 remains active in low power, but exact domain contract is unresolved. | Stock suspend/resume trace plus register/SRAM retention probes per domain. |
| UART-001 | OPEN-HW | DW APB UART parameter/probe registers, FIFO depth, DMA and busy-detect quirks? | Linux uses snps,dw-apb-uart with reg-shift 2 and 32-bit accesses. A plain 16550 model may be insufficient. | Read CPR/UCV/CTR where safe; FIFO/IRQ/timeout/overrun tests at UART0, then remaining UARTs. |
| SD-001 | OPEN-DOC | DWC MSHC revision, FIFO depth, ADMA/CQE, PHY and tuning parameters for eMMC/SDIO0/SDIO1? | Mainline compatible is thead,th1520-dwcmshc and board requests eMMC HS400. QEMU lacks this controller. | Audit Linux/vendor driver and manual; record capability registers on each instance; execute tuning and DMA error tests. |
| SD-002 | OPEN-HW | eMMC part/revision, boot partitions, RPMB, reset wiring and factory contents? | BOM and marketing say 16 GiB; actual fitted vendor and provisioning may vary. | Read CID/CSD/EXT_CSD and partition table without changing boot config or RPMB. |
| SD-003 | OPEN-HW | microSD card-detect/write-protect wiring and polarity? | DT/schematic are evidence but physical population has not been tested. | Insert/remove a sacrificial card and log GPIO/controller interrupts. |
| DMA-001 | OPEN-HW | Synopsys AXI DMAC synthesis parameters, descriptor formats, channel count and IRQ aggregation? | Mainline DT identifies axi-dma-1.01a and supplies eight channels; exact feature registers need validation. | Read identification/parameter registers and run memcpy, scatter/gather, boundary and injected-error tests. |
| GMAC-001 | OPEN-HW | GMAC 3.70a synthesis options, APB glue, queues, descriptors, checksum and timestamp behavior? | Mainline DT gives two GMACs; QEMU has no matching model. | Capability-register inventory and packet/descriptor matrix on both instances where routed. |
| PHY-001 | OPEN-HW | RTL8211F revision, strap mode, delays, reset timing and interrupt behavior? | BOM identifies RTL8211F-VD-CG; board DT requests rgmii-id and GPIO reset/IRQ. | Read PHY IDs/straps, link modes, delay configuration and reset/IRQ traces. |
| I2C-001 | OPEN-HW | DesignWare I2C synthesis parameters and TH1520 glue differences per instance? | A reusable QEMU DesignWare model exists, but parameters and DMA behavior are not established. | Read component parameter/version registers and run polling/IRQ, repeated-start, NACK, arbitration and clock-stretch tests. |
| EEPROM-001 | OPEN-DOC | Exact board EEPROM device, address, layout and factory identity fields? | Product documentation says EEPROM but current mainline board DT does not describe it. | Trace schematic/BOM and read via Linux i2c tools without writes; redact unique fields. |
| GPIO-001 | OPEN-HW | GPIO bank width, debounce, interrupt polarity/type, reset defaults and pinctrl interaction? | Mainline DT describes DW APB banks but not all synthesis/reset behavior. | Header-safe input/output loopback and interrupt matrix after checking voltage levels. |
| PIN-001 | OPEN-HW | Pinmux reset functions, pulls, drive strengths and conflicts for cape/mikroBUS headers? | DTS contains selected groups; it is not a full board pin-state oracle. | Read padctrl after reset and under stock overlays; electrically safe header loopback tests. |
| SPI-001 | OPEN-HW | DW APB SSI/QSPI revisions, FIFO, XIP mapping, NAND/NOR command and DMA behavior? | Linux DT exposes SPI0 while SoC manual maps two QSPI controllers and a boot aperture. | Capability reads; loopback; removable NOR/NAND tests; BootROM traces without touching factory flash. |
| TIMER-001 | OPEN-HW | Eight DW APB timer revisions, clock domains, EOI/IRQ and reload edge semantics? | Mainline DT maps eight instances but QEMU has no matching Synopsys model. | Register-edge and IRQ tests over stop/restart, one-shot, periodic and clock gate/reset. |
| RTC-001 | OPEN-HW | RTC epoch, width, calibration, alarm/wake and retention behavior? | Not fully represented by the current mainline board DT. | Compare against host reference across reset/suspend and alarm wake; do not alter factory calibration. |
| WDT-001 | OPEN-HW | Watchdog stages, reset domains, lock/update protocol and clock source? | Multiple AP/AO/audio watchdogs appear in the SoC map. | First test interrupt-only/non-reset mode; later run reset tests after recovery is proven. |
| ADC-001 | OPEN-HW | ADC resolution, channels, scaling and board routing? | Public product documentation is insufficient for exact analog behavior. | Apply only safe documented voltages from calibrated source after schematic review. |
| USB-001 | OPEN-DOC | Exact external connector description and board revision topology? | Official pages have historically used inconsistent connector wording; current materials indicate a micro-USB 3 host/device connection. | Confirm physical connector and schematic nets on the board in hand. |
| USB-002 | OPEN-HW | DWC3 core revision, synthesis parameters, TH1520 wrapper, PHY, IRQ and OTG role behavior? | QEMU has a reusable DWC3 host model, not a proven complete TH1520 USB block. | Read core ID/capability registers, capture enumeration/role switch and controlled error/reset behavior. |
| WIFI-001 | OPEN-HW | Exact AP6203BM/CYW43012 module revision, SDIO IDs, firmware/NVRAM and regulatory data? | BOM identifies AP6203BM; mainline DT matches cypress,cyw43012-fmac. | Read module/SDIO IDs and hash redistributable firmware/NVRAM while redacting calibration/MAC data. |
| WIFI-002 | OPEN-HW | Wi-Fi/BT power, reset, wake IRQ, UART/PCM routing and timing? | DTS shows WL_REG_ON and host wake; the complete BT side is not represented. | Trace stock driver GPIO/UART/SDIO sequence and suspend/wake events. |
| DISP-001 | OPEN-HW | DC8200/DPU and HDMI/DSI revisions, reset registers, formats, planes and IRQ behavior? | Mainline DT identifies verisilicon,dc and TH1520 DW HDMI, but no matching QEMU device exists. | Capability register dump plus mode-set traces and framebuffer checksums. |
| GPU-001 | OPEN-DOC | Is a sufficient public PowerVR BXM-4-64 programming/firmware contract available? | Product/BOM identify the GPU; exact command/MMU/firmware interfaces may be proprietary. | Inventory upstream/vendor driver and redistributable firmware; seek public specification. Keep as a fidelity blocker if unavailable. |
| MEDIA-001 | OPEN-DOC | Complete CSI/ISP/dewarp/G2D/FCE/VDEC/VENC programming contracts and board sensor population? | SoC manuals cover blocks; the base board exposes connectors but does not provide a fixed camera sensor. | Define connector backend separately from optional sensors; audit public drivers and validate deterministic streams. |
| NPU-001 | OPEN-DOC | Is the 4-TOPS NPU command ISA, firmware ABI and numeric behavior publicly implementable? | Marketing identifies the accelerator; documentation/provenance and firmware availability remain uncertain. | Inventory drivers/firmware/specification and run known tensor workloads. Missing legal/technical contract remains a blocker. |
| AUX-001 | OPEN-DOC | Exact C906 ISA/CSR/vector configuration, reset vector and firmware image? | System documentation describes a 64-bit C906 with Vector 0.7.1 for audio; current QEMU C906 lacks vector support. | Pin auxiliary firmware, public core manual and safe CSR/instruction probes after C910-mediated release. |
| AUX-002 | OPEN-DOC | Exact E902 ISA/CLIC, memory map, reset vector and low-power firmware ABI? | Documentation says RV32E[M]C, 32 interrupt sources and 32 KiB external SRAM, but not a complete integration contract. | Audit firmware/manual and capture C910-to-E902 wake/mailbox flow. |
| DSP-001 | OPEN-DOC | Vision Q7 ISA, two-instance memory/topology, firmware format and mailbox ABI? | System documentation identifies two DSPs; a legally implementable instruction model is not established. | Inventory Cadence tool/firmware ABI and public materials. Consider a firmware RPC adapter only if its limits are explicit. |
| AUDIO-001 | OPEN-HW | Audio clock tree, C906 ownership, DMA, I2S/TDM/SPDIF/VAD routing and codecs fitted on board? | SoC supports extensive audio blocks; actual board connectivity and software use need inventory. | Schematic trace, ALSA topology capture, deterministic digital loopback where electrically safe. |
| SEC-001 | OPEN-DOC | Secure boot chain, algorithms, key sources, rollback and failure visibility? | BootROM and security blocks exist, but secret keys and proprietary provisioning must never be extracted. | Use public signed-test material and invalid-image behavior; model key slots symbolically without copying secrets. |
| SEC-002 | OPEN-HW | IOPMP/firewall default ownership and violations/interrupt behavior? | Several IOPMP/security regions appear in the map; mainline Linux may not exercise them. | Safe negative accesses from controlled masters after recovery path review. |
| FUSE-001 | OPEN-HW | eFuse/OTP layout, read masks, lifecycle and unique fields? | Values can be security/privacy-sensitive and writes are irreversible. | Initially do not read undocumented locations and never write. Model documented public fields symbolically; redact unique reads. |
| BOARD-001 | OPEN-DOC | Full board-vs-SoC peripheral inventory and which interfaces are physically routed? | Mainline DTS enables a useful subset, not the complete schematic/product feature set. | Create net-to-SoC machine-readable inventory from the pinned schematic and confirm against the physical board. |
| BOARD-002 | OPEN-HW | LED/button polarity, default state and reset behavior on the board in hand? | DTS names five GPIO LEDs; marketing also describes status LEDs/buttons. | Photograph power-up state and use non-destructive GPIO/input tests. |
| HDR-001 | OPEN-HW | Exact cape/mikroBUS electrical compatibility, voltage, mux and hotplug expectations? | Similar pin layout does not imply every BeagleBone electrical behavior. | Schematic/power-rail review before any loopback; use level-safe fixture. |
| DT-001 | OPEN-DOC | Which hardware nodes/properties are missing or inaccurate in mainline versus vendor DT? | Mainline currently describes only a subset of TH1520/board blocks. | Normalize and diff all stock/vendor/mainline DTBs by image version; resolve each difference against schematic/hardware. |
| MIG-001 | OPEN-DOC | Which hardware state is volatile, retained, derived or external at migration boundaries? | Physical hardware has no VM migration contract; QEMU still needs internally consistent snapshots. | Define per-device VMState from architectural state and test save/load at every phase; document backend exclusions. |

## Resolution record template

Append a section for each resolved item:

### ID — short title

* State:
* Evidence:
* Conditions:
* Observation:
* QEMU consequence:
* Regression test:
* Residual limitation:
* Commit:

Never replace the original table question with the answer; update its state and
link to the resolution section so the history remains auditable.
