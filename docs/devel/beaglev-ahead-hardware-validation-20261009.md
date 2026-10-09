# BeagleV Ahead validation results — 2026-10-09

The tested integer, atomic, floating-point and legacy-vector result bytes match the owner's BeagleV Ahead on all four harts. The UART byte patterns also match, and the timer observations are consistent on both targets. No physical-versus-emulated instruction-result discrepancy has been found in this bounded run.

There is a confirmed test-checker portability defect and a reproduced stock-DT/OpenSBI boot compatibility limit. Several qtests intermittently fail at migration or QEMU exit, then pass on retry. Their underlying cause remains unresolved; a SIGKILL message alone does not establish a host memory kill. This bounded validation run is complete, with the physical and environment limits listed below.

The tested emulator revision is `145d3a7c8da06ee611ab0e44e4469c130bcd8ecb`, branch `beaglev-ahead`. Emulator source was not edited during the run. Raw evidence, hashes, helper sources, commands and binary outputs remain in the owner's private workspace at `beagle/captures/20261009-validation/`; they are not part of this repository. Capture paths below are relative to that private directory. Factory identifiers, credentials and network details are excluded from this published report.

## Reference preserved and UART released

The reference is THEAD C910 Release Distro 1.1.2, build `20230609164851`, Linux `5.10.113-yocto-standard`. UART captured U-Boot SPL and U-Boot `2020.01`, dated June 10, 2023. The copied OpenSBI binary contains version `v0.9-51-g540d695`; that version string does not establish a complete source mapping.

Boot kernel, DTB, firmware files, running DT, kernel configuration, partition layout, dmesg, clocks, GPIO/LED state, interrupts and MMC metadata were exported read-only. Both 4 MiB eMMC boot areas and the 2,079,744-byte `table` partition were copied and hashed. Root filesystems and Wi-Fi credentials were not exported. The boot-file and boot-area manifests provide individual SHA-256 hashes.

The 97,022-byte UART recording starts before a **software reboot**, includes shutdown, U-Boot, Linux and the login prompt, and has SHA-256 `9b678f4830e4b5f6765b75efda9bec84032e04ac27977763259a451b4e24e9b2`. It is not a cold power-on measurement. Initial stale adapter bytes are retained in the raw capture. The FT232R adapter subsequently carried clean 115200 8N1 traffic with flow control disabled.

Physical UART transmitted and received 64 KiB continuously and 4 KiB in paced bursts. Every byte matched; UART0's PLIC-source-36 interrupt count increased from 883 to 13,939. The serial getty and original printk settings were restored, the Mac port was closed, and Wi-Fi key-only SSH was verified before the owner disconnected the UART adapter. DHCP changed the address after reboot; the local SSH configuration was updated. Network time now reports the current date.

## Measured coverage

| Area | Result | Limits |
| --- | --- | --- |
| Integer/atomic/FP | **Matched:** 15,720 records and 880,336 bytes per hart, identical on four physical and four full-system Linux guest harts. | Conservative RV64GC subset; not an exhaustive ISA or privilege test. |
| Concurrent physical workload | **Matched:** four affinity-pinned processes ran simultaneously, each producing the same result hash. | Separate process outputs; no shared-memory inter-hart atomic stress test. |
| Legacy vector | **Matched:** 28 vector-length configurations, seven operation-output blocks and saturation state; 800 identical bytes on all four physical and four QEMU harts. | QEMU runs the identical ELF in U-mode through an external boot/write/exit shim. Linux signal/context behavior is excluded. |
| Portable Linux vector ABI | **Blocked:** all four guest runs return 132/SIGILL at the first legacy `vsetvli`. | Linux 6.11.9 does not enable this legacy vector state; hardware runs the vendor 5.10 kernel. This is not an arithmetic mismatch. |
| UART bytes | **Matched:** QEMU sends and validates the same 64 KiB and 4 KiB patterns in both directions; paced host-to-QEMU traffic passes. | QEMU uses a serial socket and external syscall shim. USB latency, electrical levels, error injection and the Linux UART driver are not compared. |
| Timers | **Matched within the declared observation:** zero backward reads, zero failed sleeps and zero early wakes across 32 sleeps per hart on each target. | The TIME/CLOCK_MONOTONIC ratio is consistent with 3 MHz, using a 5% median tolerance. Both depend on the kernel's time conversion; this is not an independent oscillator measurement. Host scheduling changes overshoot. |
| Physical eMMC | **Read-only reference captured:** nominal 16 GB TB2916, HS400, 8-bit bus, 1.8 V signaling, reported 198 MHz clock. | No physical write/tuning/error-injection tests. QEMU uses disposable synthetic card images of different capacity. |
| GPIO/LEDs | **Metadata captured:** six vendor Linux controllers expose 32 lines each; user LED triggers and GPIO ownership saved. | QEMU emits bank widths 32/31/32/23/23/16. Exported counts do not prove bonded silicon pin counts. No physical GPIO was driven. |
| Ethernet | **Outside physical packet scope:** driver is bound and carrier is zero; model packet/DMA checks run as qtests. | Cable remains disconnected. No hardware link, packets, DMA or PHY negotiation claim. |
| Wi-Fi | **Working hardware transport:** association, key-only SSH and file transfers verified, including return after reboot. SDIO/GPIO metadata saved. | QEMU has control/wake support but no Wi-Fi SDIO data path. RF/association behavior cannot match an absent model. |

The shared scalar result SHA-256 is `cf891df2c8b7af547b3c9574a0801b9d68976828dd42ebf51f53556cb80a2c11`. It covers 6,400 integer, 2,560 atomic and 6,760 double-precision floating-point records, including five rounding modes and exception flags. The vector result SHA-256 is `284654d69741c25f9d4f25d357db867ad05aeff6022841b8bcca7803ba7cb92e`.

## Emulator regression gates

Both the normal full `riscv64-softmmu` build and a separate LLVM 22.1.8 ASan/UBSan build succeeded. The first sanitizer configuration mixed Apple clang 16 C objects with the LLVM Objective-C linker and failed at ASan runtime linkage. Explicit compiler paths fixed the build configuration without changing QEMU source. Runtime checks disable leak detection and halt on ASan/UBSan errors; no memory/undefined-behavior diagnostic has been observed in completed runs.

Both hash-pinned eMMC functional tests passed in **both builds**: root boot, write/sync/hash, read-only remount, fresh-process reopening and the four-hart storage case. These use Linux 6.11.9 and the declared portable rootfs. The tests report through `/dev/kmsg` and earlycon because runtime UART probing remains deferred.

All **22 existing firmware executables exit successfully in both builds**. Their complete check totals are **21/22** because `check-thead-c910-sync-ir.py` expects `th.sync*` mnemonics while Capstone 5.0.9 prints their raw bytes. The saved IR contains `mb seq:all`, PC advance and TB exit for all four opcodes. A separate diagnostic annotation passes the unchanged checker; it is not counted as an unmodified-checker pass.

The normal device run attempted all **175 qtest cases**: **168 initially passed**, one reached a migration deadline, and six ended with a QEMU SIGKILL message. Targeted retries eventually passed **all 175 unique cases**. The sanitizer selection attempted **77 cases** covering boot contracts, GMAC, eMMC/SDIO, PLIC, CLINT and UART: **76 initially passed**, with one UART-instance SIGKILL failure; its retry passed, giving **77 unique passing cases**. There were no selected-case skips. These are combined coverage totals, not a single clean suite run. Raw failures and all retries remain available in the qtest gate summary (`qemu/qtest-gates.json`, private capture).

The affected cases were `boot/mask-rom-contract`, `ddr/registers`, `usb/registers`, `dw-wdt/action-reset`, `dwcmshc/registers`, `dwcmshc/sd-cmd19-tuning`, `dwcmshc/emmc-hs400-profile`, and sanitized `dw-uart/instances`. The SIGKILL failures took about 30 seconds. The unchanged test library sends SIGTERM during cleanup, waits up to 30 seconds, then sends SIGKILL itself. A later attempt to sample live QEMU stacks found that each selected retry had already exited successfully. No stalled stack or independent OS kill evidence was obtained, so shutdown hangs, host scheduling/resource effects and their root cause remain unresolved. Earlier raw progress logs labeled these as host kills; this report and the structured result metadata correct that attribution.

## Inventory and stock boot boundary

The structured DT summaries (`comparisons/`, private capture) compare the captured hardware DT with QEMU's mainline and vendor clock ABI variants.

| Field | Stock hardware image | QEMU | Attribution |
| --- | --- | --- | --- |
| Root compatibility | `beagle,light`, `thead,light-val`, `thead,light` | `beagle,beaglev-ahead`, `thead,th1520` | Firmware/DT generation. |
| DT RAM | Base 2 MiB, size `0xffe00000` | Base zero, size 4 GiB | Firmware reservation versus physical model contract. |
| Linux RAM | Boot explicitly ignores `0x200000–0x40200000`; MemTotal about 2.9 GiB | Portable guest MemTotal about 3.9 GiB | Stock kernel placement excludes the lowest 1 GiB, plus kernel/reserved allocations. Not evidence that QEMU has the wrong physical RAM size. |
| Reservations | M-mode, TEE, DSP, video/facelib and 320 MiB CMA descriptions | Generic firmware reservation; unsupported auxiliary subsystems omitted | Different firmware and implemented subsystem scope. Several stock no-map regions lie in the already-excluded low range. |
| Timebase | 3,000,000 | 3,000,000 | Metadata and bounded timer consistency agree. |
| UART0 clock | `thead,light-fm-ree-clk`, ID 469, name `baudclk` | Mainline `thead,th1520-clk-ap`; vendor ABI uses a fixed UART clock | Older stock binding is not the supported RevyOS binding. |
| UART0 aperture | `0x4000` bytes | Primary generated node `0x100` bytes | DT/model aperture difference; physical reserved-access behavior unmeasured. |
| eMMC binding | `snps,dwcmshc-sdhci`, fixed core clock, extra four-byte register resource | TH1520 DWC MSHC compatibles; mainline clock ID 43 or vendor ID 122 | Kernel/binding difference. |
| MMC aliases | Stock publishes mmc0/mmc1; Wi-Fi becomes mmc2 | Explicit mmc0/mmc1/mmc2 aliases | Boot naming contract differs; IRQ 62/64/71 routing agrees in metadata. |

Four bounded stock boot attempts were saved. With QEMU's generated mainline or vendor DT, the copied stock kernel prints its Linux banner and waits for `/dev/mmcblk0p3`. No physical root partition is attached, so that checkpoint cannot establish complete official-image boot.

With the original stock DT and bundled generic OpenSBI 1.8.1, firmware fails before Linux on a store at `0xffdc004000`. Disassembly of the faulting firmware PC `0x22034` confirms `sd a1, 0(a2)`, a 64-bit store. The DT selects `riscv,clint0`; the C900 model accepts only 32-bit accesses. The mapped register address exists. This is a reproduced **DT/firmware/model access-contract incompatibility**, not yet evidence that real TH1520 hardware accepts the generic access. The instruction bytes and firmware hash are saved in the CLINT triage record (`qemu/stock-boot/clint-access-triage.json`, private capture). The original stock OpenSBI binary produced no console output in its bounded direct-load attempt.

## Findings and next fixes

1. Make the sync IR checker recognize instruction words as well as disassembler mnemonics. The executable and ordering IR already pass; no sync behavior change is justified by this run.
2. Reproduce and sample the intermittent migration/exit failures under a controlled host load. `qtest_wait_qemu()` escalates to SIGKILL after 30 seconds, so the signal alone does not prove a macOS memory kill or a device-model assertion failure. All affected cases passed on retry; the stability gate remains open.
3. Add a separately named stock-image compatibility lane. Preserve the older `light` binding and OpenSBI identity, then resolve the CLINT access contract using physical/vendor source evidence before changing widths or aliases.
4. Use a supported vendor guest kernel for Linux legacy-vector context and signal tests. The identical U-mode arithmetic bytes now match; kernel context preservation remains untested.

Relevant uncertainty-ledger entries remain open: `DOC-002a/b` for complete factory-image/source identity; `BOOT-001/003/005` for cold/reset/ROM semantics; `CPU-001/002/003/006/014/016` for privileged identification, full ISA, vector edge/context and FXCR behavior; `MEM-001`, `CLK-001/003`, `UART-001/002`, `SD-001/002`, `GPIO-001`, `TIMER-001`, and `WIFI-001/002` for the unmeasured physical details. A software inventory match or a passing synthetic regression does not close those items.

Cold power-on capture, exact PCB/adapter electrical measurements, a matching Linux vector ABI, physical Ethernet traffic and privileged/register/reset probes remain outside this completed UART session. Final key-only Wi-Fi SSH verified the stock kernel, active supplicant/SSH/getty, restored printk settings and zero Ethernet carrier. No Beagle validation QEMU process remained running. The current reference can be accessed with `ssh -F beagle/ssh_config beagle`; reconnect UART at 115200 8N1 if Wi-Fi recovery is needed.

The machine-readable coverage assessment (`comparisons/coverage.json`, private capture) records matched, mismatched, blocked and outside-scope outcomes with their ledger links. The capture manifest (`manifest.json`, private capture) hashes the preserved evidence and helper sources. QEMU source and commit remained unchanged throughout; no implementation fixes were made. This redacted report was prepared for publication after the run.

## Evidence archive identity

The private capture manifest records 752 files totaling 280,095,596 bytes.
Its SHA-256 is `5f82edd18f682d22f01ce3711541c4302012c1b719e7c470c997488a1561578d`.
It anchors the archived evidence without publishing factory identifiers or
network configuration. The validation plan is in
[the companion plan](beaglev-ahead-hardware-validation-plan.md).
