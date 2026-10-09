# BeagleV Ahead mainline QEMU boot — 2026-10-09

Current Torvalds mainline booted on the unchanged BeagleV Ahead model with both
QEMU's generated device tree and the kernel's upstream Ahead device tree.
Both runs brought up four CPUs, mounted the disposable eMMC root, completed
the scalar and timer workloads, and printed through the runtime UART.
The physical board remains on `5.10.113-yocto-standard`; no hardware boot,
storage or boot-environment changes were made for this test.

The subsequent [physical mainline RAM boot](beaglev-ahead-mainline-hardware-20261009.md)
uses this exact kernel and configuration and records the separate hardware
handover, CPU/timer/UART observations and recovery boundary.

## Exact build

* Linux source: Torvalds mainline fetched on 2026-10-09, commit
  `af32da41b0327b9c6a37856ba82b6760d6c8d10e`
  ([source commit](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=af32da41b0327b9c6a37856ba82b6760d6c8d10e)).
* Kernel release: `7.3.0-rc6-beagle-qemu-20261009+`; source tree clean, no kernel patches.
* Compiler: Homebrew clang/LLD 22.1.8 on Apple Silicon macOS.
* Configuration: a small TH1520 configuration, with four CPUs, built-in clock,
  pinctrl, UART, GPIO, MMC and ext filesystem drivers. The exact resolved
  configuration is [saved alongside this report](beaglev-ahead-mainline-20261009.config).
  It is a boot-test configuration, not a full board distribution or network image.
* QEMU executable: built from
  `145d3a7c8da06ee611ab0e44e4469c130bcd8ecb`; SHA-256
  `f88a58139b193c434e31370776585d689b4e5c2af6fc5e7839e6a228cb95b7cf`.
  The subsequent `67e9cfd100` commit changed validation documentation only.

The kernel image SHA-256 is
`e154599b4b81505364f96337bcd9fee0eb924c33b0709760eb0030391687c54c`.
The upstream Ahead DTB SHA-256 is
`468b918d468253815edd0300f4199f3d00212db240521e96088edd7c10d02c36`.

From the workspace, `scripts/env.sh` was sourced before make. The isolated,
case-sensitive source worktree and out-of-tree build were used as follows:

```sh
make -C "$LINUX_SOURCE" O="$LINUX_BUILD" ARCH=riscv LLVM=1 \
    CROSS_COMPILE=riscv64-linux-gnu- -j2 \
    Image thead/th1520-beaglev-ahead.dtb
```

`LINUX_SOURCE` and `LINUX_BUILD` designate the isolated source and build
paths. To recreate the configuration, copy the saved config to the build's
`.config` and run `olddefconfig` with the same ARCH/LLVM/cross-prefix arguments.

## Measured results

| Check | QEMU-generated DT | Upstream Ahead DT |
| --- | --- | --- |
| Kernel, four CPUs and userspace init | Passed | Passed |
| Disposable eMMC root and read-only remount | Passed | Passed |
| Scalar/atomic/FP output, all four harts | Identical to hardware | Identical to hardware |
| Timer monotonicity and 32 sleeps per hart | Passed | Passed |
| Runtime `ttyS0` output | Passed | Passed |
| Legacy vector Linux workload | SIGILL/132 | SIGILL/132 |
| Deferred-device list at capture | Empty | Wi-Fi SDIO controller |

Every scalar hart output is 880,336 bytes / 15,720 records and has SHA-256
`cf891df2c8b7af547b3c9574a0801b9d68976828dd42ebf51f53556cb80a2c11`,
matching the physical outputs from
[the first hardware comparison](beaglev-ahead-hardware-validation-20261009.md).
The identical ELF was used; it was not recompiled for mainline.

Timer reads never moved backwards; all sleeps returned successfully with no
early wakes. The median TIME/CLOCK_MONOTONIC ratio was within the declared 5%
tolerance around 3 MHz. This is a kernel time-conversion consistency check,
not an independent oscillator measurement. The previously pinned Linux
6.11.9 fixture had deferred runtime UART probing; this new configuration binds
`ttyS0` and accepts an actual write to it. Kernel and configuration both differ,
so this alone does not identify the old fixture's probe cause.

The build enables `CONFIG_RISCV_ISA_XTHEADVECTOR`,
`CONFIG_ERRATA_THEAD_GHOSTWRITE` and CPU mitigations. Each vector payload
returns 132 at its first legacy `vsetvli`, with only its eight-byte header
written. This is consistent with the kernel's deliberate C9xx vector
mitigation ([pinned kernel code](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/arch/riscv/kernel/bugs.c?id=af32da41b0327b9c6a37856ba82b6760d6c8d10e)).
No mitigation overrides were used. The earlier identical-ELF U-mode comparison
through an external QEMU shim already matched the hardware's vector bytes;
Linux vector context behavior remains blocked in these boot runs.

The root fixture is the same hash-pinned portable image used by
`tests/functional/riscv64/test_beaglev_ahead.py`, SHA-256
`b6ed95610310b7956f9bf20c4c9c0c05fea647900df441da9dfe767d24e8b28b`.
It was expanded into separate disposable 128 MiB partitioned eMMC images.
This kernel mounts the ext2-format root through its ext4 driver. Test outputs
were saved, synced, remounted read-only and extracted after QEMU exited.
The hardware's factory root and Wi-Fi credentials were not involved.

The QEMU boot arguments for each lane were:

```sh
qemu-system-riscv64 -M beaglev-ahead -display none -monitor none \
    -serial file:console.log -kernel Image \
    -drive file=emmc.img,if=sd,index=0,format=raw \
    -append 'console=ttyS0,115200 earlycon root=PARTUUID=1520a110-01 rootwait rw panic=-1 init=/bin/sh -- /validation-init.sh' \
    -no-reboot
```

The second lane additionally supplied `-dtb th1520-beaglev-ahead.dtb`.
The local helper installed the init script and unchanged test ELFs in each
fixture, captured inventories/results, and stopped QEMU after the completion
marker. Full commands, console logs, helper sources, binary outputs and hashes
remain in the private workspace's `beagle/captures/20261009-mainline-qemu/`.
The two bounded workload runs each took approximately 4.3 seconds.

## Hardware boot and recovery boundary

These are full-system QEMU kernel boots, not proofs of the physical mask-ROM,
factory SPL/U-Boot/OpenSBI chain, stock DT or microSD boot path. In particular,
the older stock DT/generic OpenSBI CLINT access incompatibility remains recorded
in the first report; the successful upstream DT does not resolve it.

The board documentation describes an SD boot button, but an April 2026
[maintainer report](https://forum.beagleboard.org/t/beaglev-ahead-sd-card-boot-issue/43885)
says the official Ahead images were verified only for eMMC boot. A standalone
SD image needs to be demonstrated before it is treated as independent recovery.
Erasing eMMC to force SD boot is not part of this plan.

The documented [USB download/reflash procedure](https://docs.beagleboard.org/boards/beaglev/ahead/02-quick-start.html#flashing-emmc)
provides recovery for a damaged software installation without a booting Linux
system. It overwrites eMMC contents and does not repair physically failed
storage. The detailed published host-tool instructions use Linux; this session
has not demonstrated a Mac flashing workflow.

For a later physical test, restore UART access and verify a compatible recovery
image first. Prefer a temporary bootloader load of a separate kernel, DT and
initramfs into RAM from microSD, using volatile boot settings and preserving
the default eMMC image. This path still requires validation with the factory
bootloader/firmware. No physical test or eMMC reflash was performed here.
