# BeagleV Ahead QEMU hardware validation plan

Validate the current `qemu-th1520` implementation against the owner's BeagleV Ahead, using Wi-Fi for SSH and file transfers and the 115200 8N1 UART for console evidence. Ethernet stays disconnected. Keep the emulator at commit `145d3a7c8da06ee611ab0e44e4469c130bcd8ecb` on `beaglev-ahead` while collecting results; record discrepancies before considering fixes.

The first useful result is a reproducible inventory comparison followed by identical CPU test binaries on both targets. Existing QEMU regression tests establish emulator behavior, while physical measurements establish which assumptions match this board.

## Current hardware baseline

The stock image boots THEAD C910 Release Distro 1.1.2, build `20230609164851`, with kernel `5.10.113-yocto-standard`. Key-only root SSH over Wi-Fi has been verified, and the SSH host key was obtained directly over UART. The following command refers to the owner's local sibling `beagle/` directory, whose SSH configuration and raw captures remain outside this repository:

```sh
ssh -F beagle/ssh_config beagle
```

The address in `ssh_config` is the current DHCP lease; update it if the lease changes. UART provides access independently of Wi-Fi. The initial stock clock reported 2023; network time is now current. Test assets were obtained and hash-verified on the Mac. The executed measurements are recorded in [the validation results](beaglev-ahead-hardware-validation-20261009.md).

| Observation | Current QEMU contract | Interpretation |
| --- | --- | --- |
| Four Linux processors, hart IDs 0–3 | Four C910 harts | Topology agrees; instruction behavior still needs tests. |
| Vendor Linux reports Sv39 and vector version 0.7.1 | Sv39 and XTheadVector derived from 0.7.1 | Compatible starting point; the kernel's report does not prove individual instruction semantics. |
| Boot DT timer frequency is 3,000,000 Hz | `TH1520_TIMEBASE_FREQ` is 3,000,000 | DT metadata agrees; timer rate and interrupt behavior need observation. |
| Boot DT memory starts at 2 MiB and spans `0xffe00000` bytes | Physical RAM begins at zero and spans 4 GiB | Investigate firmware reservations and handoff DT; these describe different boot stages. |
| Linux sees 2,993,620 KiB of memory | 4 GiB physical RAM | Capture all reserved regions before attributing the difference. |
| Vendor Linux exports six GPIO controllers with 32 lines each | QEMU exposes 32, 31, 32, 23, 23 and 16 lines | A concrete vendor/mainline interface difference; exported counts alone do not establish bonded pin counts or silicon capability. |
| UART0, eMMC and Wi-Fi SDIO use PLIC sources 36, 62 and 71 | Corresponding model routes use 36, 62 and 71 | Initial routing metadata agrees. |

Raw local evidence is in the private workspace directory `beagle/captures/20261009-bringup/`, outside this repository. The first baseline's BusyBox `od` commands rejected GNU options; the memory and timer fields were recovered using `od -b -v` in the follow-up Wi-Fi inspection capture. Keep the raw captures private.

## 1 Preserve the reference and capture the stock boot

Record the QEMU commit, build options, compiler versions and binary SHA-256. Record the board's kernel config, boot DTB, command line, reserved memory, firmware inventory, eMMC partition layout and bootloader version. Hash exported files and record unknown source revisions explicitly.

Capture a complete normal boot over UART during the next owner-initiated restart, starting before power-on. Verify Wi-Fi and SSH return afterwards. The earlier captures start after boot and do not establish the bootloader sequence.

Use SSH to copy read-only files such as `/sys/firmware/fdt` and `/proc/config.gz` when present. Copy boot partitions or image files read-only after checking their layout and storage budget; do not overwrite the board's factory storage. Record the already-authorized SSH and Wi-Fi configuration changes alongside the stock firmware identity.

Acceptance: immutable captures and hashes, a mapped partition layout, and a known way to recover the UART console if Wi-Fi drops.

## 2 Establish the unchanged emulator baseline

Build the full `riscv64-softmmu` target in a separate build directory, following the workspace's `scripts/env.sh` instructions before `make`. Start with a normal build; add sanitizer coverage after the basic gates pass.

Run the existing board qtests, Ahead-specific TCG tests, and `tests/functional/riscv64/test_beaglev_ahead.py`. Record executed, skipped and failed tests rather than substituting historical totals from the project documents.

The board qtest entry point, from a configured build directory, is:

```sh
./pyvenv/bin/meson test --print-errorlogs 'qtest-riscv64/beaglev-ahead-test'
```

The functional test has hash-pinned kernel/rootfs assets and checks eMMC root boot, single/four-hart operation, data hashing and fresh-process reopening. Its mainline kernel's runtime UART is deferred; success messages use `/dev/kmsg` and earlycon. Treat that as a recorded console limitation.

The fresh checkout has no build products or downloaded guest assets. Recover or download the declared fixtures before running these gates. Use disposable disk images for QEMU storage tests.

Acceptance: reproducible test results on the frozen commit, including the exact failing or skipped cases and asset hashes.

## 3 Compare Linux inventories and the stock image boundary

Compare DT compatibility strings, memory/reserved regions, CPU properties, clock descriptions, interrupt routes, GPIO counts, storage aliases and bound drivers. Compare the captured stock DT against both QEMU clock ABI variants; `clock-abi=vendor` describes the supported RevyOS binding and does not automatically imply compatibility with the older stock image's `light` bindings.

Attempt the copied stock kernel and supporting images in QEMU with unchanged emulator code. Preserve the full failure checkpoint if it cannot boot. The project currently reports that an unmodified official board image does not boot, so this is a compatibility measurement, not a prerequisite for every subsequent CPU test.

When using the already-supported portable QEMU image, label its kernel and DT differences explicitly. Driver differences between that image and the stock board image are not sufficient evidence of an emulator defect.

Acceptance: a structured difference table with each item attributed to firmware, kernel/DT, model behavior or an unresolved cause.

## 4 Run identical CPU workloads on both targets

Start with small static Linux executables using a conservative RV64GC ABI. Use the same ELF and test inputs on hardware and inside the full-system `beaglev-ahead` guest. Record its SHA-256, structured results, exit status and signals. Test every physical hart using affinity.

Prioritize scalar integer/atomic and floating-point results, then short XTheadVector sequences: vector length, element operations, reductions, masks, permutations, overlap and saturation. Use the branch's legacy instruction encodings rather than standard RVV 1.0 encodings. Keep sizes bounded and handle illegal-instruction outcomes explicitly.

The existing `tests/tcg/riscv64/test-xtheadvector*.S` and C910 tests are useful references, but their M-mode CSR access, trap setup and semihosting prevent execution as ordinary stock-Linux programs. Prepare external U-mode harnesses for safe instruction subsets; leave privileged probes for a later removable-media boot with an approved recovery procedure.

Acceptance: identical deterministic result bytes, flags and expected traps, or a small isolated discrepancy reproduced on both targets. Kernel-dependent signal/context tests require matching or explicitly controlled kernels.

## 5 Validate peripherals available with the current connections

| Area | Test now | Boundary |
| --- | --- | --- |
| UART0 | Console input/output, bounded byte transfers, hashes, idle/burst behavior and interrupt counts; compare against QEMU's UART backend. | Character results and error handling are comparable; USB adapter latency and emulator host scheduling are not silicon timing. |
| Timers and SMP | Monotonic counters, sleep/wakeup ordering, timer interrupts and bounded multi-hart workloads. | Use tolerances for host scheduling; metadata alone does not prove timer behavior. |
| eMMC | Read-only partition, capacity and boot-file hashes; compare driver-visible capabilities and transfer results. | Writes use disposable QEMU images; physical storage writes require a separately agreed test area. |
| GPIO and LEDs | Controller inventory, existing LED trigger state and DT wiring. | Read metadata first; GPIO driving, reset and loopbacks require pin ownership and voltage checks. |
| Ethernet | QEMU device qtests and simulated-backend checks; physical driver/DT inventory and no-carrier state. | Hardware packet/DMA/link behavior remains unverified while the cable is absent. |
| Wi-Fi | SSH and file-transfer transport on the real board; observe SDIO/GPIO metadata. | QEMU has a control/wake peer but no SDIO Wi-Fi data path; association and RF behavior have no emulated counterpart. |

USB host tests, external buses and privileged register/reset probes are later additions when their connections and recovery method are available. Wi-Fi throughput is a transport check, not a QEMU networking fidelity score.

## 6 Record and assess every result

Store each run outside the QEMU source tree with the frozen commit, test source and binary hashes, kernel/DT hashes, UART parameters, exact commands, raw logs and parsed results. Use private captures for addresses and factory identity; publish only redacted summaries.

Use verdicts of matched, mismatched, blocked or outside current scope. Separate repeated emulator discrepancies from firmware/kernel differences and measurement artifacts. Tie findings back to the [existing uncertainty ledger](beaglev-ahead-hardware-validation.md); an inventory match does not close an item requiring register, reset or execution evidence.

The first deliverable is the inventory difference table plus one deterministic CPU workload executed unchanged on all four physical harts and in the full-system guest. End with a measured coverage report and proposed fixes, keeping this validation run's emulator unchanged.
