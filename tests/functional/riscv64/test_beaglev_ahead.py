#!/usr/bin/env python3
#
# Functional test that boots Linux from the BeagleV Ahead eMMC model
#
# SPDX-License-Identifier: GPL-2.0-or-later

import logging
import os
import shutil
import struct
from subprocess import DEVNULL, check_call

from qemu_test import (Asset, LinuxKernelTest, get_qemu_img,
                       wait_for_console_pattern)


class BeagleVAhead(LinuxKernelTest):

    ASSET_KERNEL = Asset(
        'https://storage.tuxboot.com/kernels/6.11.9/riscv64/Image',
        '174f8bb87f08961e54fa3fcd954a8e31f4645f6d6af4dd43983d5e9841490fb0')
    ASSET_ROOTFS = Asset(
        ('https://github.com/groeck/linux-build-test/raw/'
         '9819da19e6eef291686fdd7b029ea00e764dc62f/rootfs/riscv64/'
         'rootfs.ext2.gz'),
        'b6ed95610310b7956f9bf20c4c9c0c05fea647900df441da9dfe767d24e8b28b')

    PAYLOAD_SHA256 = ('fb5ac1fab9c5b0e2b328bbe2149dee4664cf064618884f78c'
                      '10e5c3bf6a05cda')
    DISK_SIGNATURE = 0x1520A110
    PARTUUID = f'{DISK_SIGNATURE:08x}-01'
    SMP_FAILURE_MESSAGES = (
        'failed to come online',
        'failed to start',
        'soft lockup',
        'Failed to initialize a non-removable card',
        'whilst initialising MMC card',
        'card never left busy state',
        'Timeout waiting for hardware interrupt.',
        'I/O error, dev mmcblk',
        'Cannot open root device',
        'unknown-block',
        'Kernel panic - not syncing',
        'SMP_EMMC_WRITE_FAIL',
        'SMP_EMMC_PROCESS_REOPEN_FAIL',
    )

    def _launch_emmc(self, name, kernel_path, rootfs_path, init_script,
                     maxcpus, disk_format, root_device):
        vm = self.get_vm(name=name)
        vm.set_console()
        # This kernel leaves the runtime UART deferred for an unresolved
        # reason.  The init script therefore reports through /dev/kmsg, which
        # earlycon exposes to the test.
        cpu_limit = f'maxcpus={maxcpus} ' if maxcpus else ''
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200 earlycon ' + cpu_limit +
                               f'root={root_device} rootwait rw '
                               'panic=-1 init=/bin/sh -- -c ' +
                               init_script)
        vm.add_args('-kernel', kernel_path,
                    '-append', kernel_command_line,
                    '-drive', (f'file={rootfs_path},if=sd,index=0,'
                               f'format={disk_format}'),
                    '-no-reboot')

        vm.launch()
        return vm

    def _boot_emmc(self, name, kernel_path, rootfs_path, init_script):
        # Keep secondary-hart bring-up outside this storage-specific regression
        # test.  The separate SMP acceptance test below exercises all harts.
        vm = self._launch_emmc(name, kernel_path, rootfs_path, init_script,
                               maxcpus=1, disk_format='raw',
                               root_device=f'PARTUUID={self.PARTUUID}')
        self.wait_for_console_pattern('new HS400 MMC card', vm=vm)
        self.wait_for_console_pattern('VFS: Mounted root (ext2 filesystem)',
                                      vm=vm)
        return vm

    def _wait_for_smp_console(self, vm, success_message):
        console = vm.console_file
        console_logger = logging.getLogger('console')

        while True:
            line = console.readline()
            if not line:
                self.fail(f"EOF in console, expected '{success_message}'")

            message = line.decode(errors='replace').strip()
            console_logger.debug(message)
            for failure_message in self.SMP_FAILURE_MESSAGES:
                if failure_message in message:
                    self.fail(f"'{failure_message}' found in console, "
                              f"expected '{success_message}'")
            if success_message in message:
                return

    def _partition_rootfs(self, rootfs_path, disk_name):
        # Linux probes the three MMC hosts asynchronously, while an MBR
        # PARTUUID is stable and available before userspace starts.
        sector_size = 512
        partition_start = 2048
        rootfs_size = os.path.getsize(rootfs_path)
        self.assertEqual(rootfs_size % sector_size, 0)
        partition_sectors = rootfs_size // sector_size
        disk_path = self.scratch_file(disk_name)
        minimum_size = (partition_start + partition_sectors) * sector_size
        disk_size = 1 << (minimum_size - 1).bit_length()

        mbr = bytearray(sector_size)
        struct.pack_into('<I', mbr, 440, self.DISK_SIGNATURE)
        struct.pack_into('<B3sB3sII', mbr, 446,
                         0, b'\xfe\xff\xff', 0x83, b'\xfe\xff\xff',
                         partition_start, partition_sectors)
        struct.pack_into('<H', mbr, 510, 0xaa55)

        with open(disk_path, 'wb') as disk:
            # The eMMC model requires a power-of-two backing size.
            disk.truncate(disk_size)
            disk.write(mbr)
            disk.seek(partition_start * sector_size)
            with open(rootfs_path, 'rb') as rootfs:
                shutil.copyfileobj(rootfs, disk)

        return disk_path

    def test_emmc_root(self):
        self.set_machine('beaglev-ahead')
        kernel_path = self.ASSET_KERNEL.fetch()
        rootfs = self.uncompress(self.ASSET_ROOTFS)
        rootfs_path = self._partition_rootfs(rootfs, 'root.img')

        # The scripts contain no literal spaces because the kernel splits the
        # text following "--" into init arguments.  IFS supplies separators at
        # runtime.  EMPTY splits each success marker so the marker cannot be
        # matched in the kernel's printed command line before init emits it.
        write_script = (
            '/bin/mount${IFS}-t${IFS}proc${IFS}proc${IFS}/proc&&'
            '/bin/echo${IFS}BEAGLEV_AHEAD_EMMC_ROOT_SHELL_'
            '${EMPTY}PASS>/dev/kmsg&&'
            '/usr/bin/yes${IFS}BEAGLEV-AHEAD-EMMC-INTEGRITY|'
            '/usr/bin/head${IFS}-c${IFS}1048576>/emmc-integrity.bin&&'
            '/bin/sync&&'
            '/usr/bin/sha256sum${IFS}/emmc-integrity.bin>/dev/kmsg&&'
            '/bin/mount${IFS}-o${IFS}remount,ro${IFS}/&&'
            '/bin/echo${IFS}EMMC_WRITE_SYNC_${EMPTY}PASS>/dev/kmsg||'
            '/bin/echo${IFS}EMMC_WRITE_SYNC_${EMPTY}FAIL>/dev/kmsg;'
            'exec${IFS}/bin/sleep${IFS}3600')
        verify_script = (
            '/bin/mount${IFS}-t${IFS}proc${IFS}proc${IFS}/proc&&'
            '/usr/bin/sha256sum${IFS}/emmc-integrity.bin>/dev/kmsg&&'
            '/bin/mount${IFS}-o${IFS}remount,ro${IFS}/&&'
            '/bin/echo${IFS}EMMC_PROCESS_REOPEN_${EMPTY}PASS>/dev/kmsg||'
            '/bin/echo${IFS}EMMC_PROCESS_REOPEN_${EMPTY}FAIL>/dev/kmsg;'
            'exec${IFS}/bin/sleep${IFS}3600')

        vm = self._boot_emmc('write', kernel_path, rootfs_path, write_script)
        wait_for_console_pattern(
            self, 'BEAGLEV_AHEAD_EMMC_ROOT_SHELL_PASS',
            failure_message='EMMC_WRITE_SYNC_FAIL', vm=vm)
        wait_for_console_pattern(self, self.PAYLOAD_SHA256,
                                 failure_message='EMMC_WRITE_SYNC_FAIL',
                                 vm=vm)
        wait_for_console_pattern(self, 'EMMC_WRITE_SYNC_PASS',
                                 failure_message='EMMC_WRITE_SYNC_FAIL',
                                 vm=vm)
        # QMP quit fully closes this process after sync and a read-only
        # remount.  The next VM tests a fresh-QEMU reopen, not power loss or
        # host-cache eviction.
        vm.shutdown()

        vm = self._boot_emmc('verify', kernel_path, rootfs_path, verify_script)
        wait_for_console_pattern(self, self.PAYLOAD_SHA256,
                                 failure_message='EMMC_PROCESS_REOPEN_FAIL',
                                 vm=vm)
        wait_for_console_pattern(self, 'EMMC_PROCESS_REOPEN_PASS',
                                 failure_message='EMMC_PROCESS_REOPEN_FAIL',
                                 vm=vm)
        vm.shutdown()

    def test_emmc_root_smp(self):
        self.set_machine('beaglev-ahead')
        kernel_path = self.ASSET_KERNEL.fetch()
        rootfs = self.uncompress(self.ASSET_ROOTFS,
                                 target='smp-root.ext2')
        raw_disk = self._partition_rootfs(rootfs, 'smp-root.img')
        overlay = self.scratch_file('smp-root.qcow2')
        qemu_img = get_qemu_img(self)

        # Preserve the pinned rootfs as a fresh backing image while allowing
        # the first QEMU process to persist writes for the reopen check.
        check_call([qemu_img, 'create', '-f', 'qcow2', '-b', raw_disk,
                    '-F', 'raw', overlay], stdout=DEVNULL, stderr=DEVNULL)

        # As in the single-hart test, IFS keeps the shell program in one kernel
        # argument; EMPTY prevents command-line success-marker false matches.
        write_script = (
            '/bin/mount${IFS}-t${IFS}proc${IFS}proc${IFS}/proc&&'
            '/bin/mount${IFS}-t${IFS}sysfs${IFS}sysfs${IFS}/sys&&'
            '/bin/grep${IFS}-qx${IFS}0-3${IFS}'
            '/sys/devices/system/cpu/online&&'
            '/bin/echo${IFS}SMP_EMMC_FOUR_HARTS_${EMPTY}PASS>/dev/kmsg&&'
            '/usr/bin/yes${IFS}BEAGLEV-AHEAD-EMMC-INTEGRITY|'
            '/usr/bin/head${IFS}-c${IFS}1048576>'
            '/smp-emmc-integrity.bin&&'
            '/bin/sync&&'
            '/usr/bin/sha256sum${IFS}/smp-emmc-integrity.bin>/dev/kmsg&&'
            '/bin/mount${IFS}-o${IFS}remount,ro${IFS}/&&'
            '/bin/echo${IFS}SMP_EMMC_WRITE_${EMPTY}PASS>/dev/kmsg||'
            '/bin/echo${IFS}SMP_EMMC_WRITE_${EMPTY}FAIL>/dev/kmsg;'
            'exec${IFS}/bin/sleep${IFS}3600')
        verify_script = (
            '/bin/mount${IFS}-t${IFS}proc${IFS}proc${IFS}/proc&&'
            '/bin/mount${IFS}-t${IFS}sysfs${IFS}sysfs${IFS}/sys&&'
            '/bin/grep${IFS}-qx${IFS}0-3${IFS}'
            '/sys/devices/system/cpu/online&&'
            '/bin/echo${IFS}SMP_EMMC_REOPEN_FOUR_HARTS_'
            '${EMPTY}PASS>/dev/kmsg&&'
            '/usr/bin/sha256sum${IFS}/smp-emmc-integrity.bin>/dev/kmsg&&'
            '/bin/mount${IFS}-o${IFS}remount,ro${IFS}/&&'
            '/bin/echo${IFS}SMP_EMMC_PROCESS_REOPEN_'
            '${EMPTY}PASS>/dev/kmsg||'
            '/bin/echo${IFS}SMP_EMMC_PROCESS_REOPEN_'
            '${EMPTY}FAIL>/dev/kmsg;'
            'exec${IFS}/bin/sleep${IFS}3600')

        vm = self._launch_emmc('smp-write', kernel_path, overlay, write_script,
                               maxcpus=None, disk_format='qcow2',
                               root_device=f'PARTUUID={self.PARTUUID}')
        for pattern in ('smp: Brought up 1 node, 4 CPUs',
                        'new HS400 MMC card',
                        'VFS: Mounted root (ext2 filesystem)',
                        'SMP_EMMC_FOUR_HARTS_PASS',
                        self.PAYLOAD_SHA256,
                        '): re-mounted ',
                        'SMP_EMMC_WRITE_PASS'):
            self._wait_for_smp_console(vm, pattern)
        vm.shutdown()

        vm = self._launch_emmc('smp-verify', kernel_path, overlay,
                               verify_script, maxcpus=None,
                               disk_format='qcow2',
                               root_device=f'PARTUUID={self.PARTUUID}')
        for pattern in ('smp: Brought up 1 node, 4 CPUs',
                        'new HS400 MMC card',
                        'VFS: Mounted root (ext2 filesystem)',
                        'SMP_EMMC_REOPEN_FOUR_HARTS_PASS',
                        self.PAYLOAD_SHA256,
                        '): re-mounted ',
                        'SMP_EMMC_PROCESS_REOPEN_PASS'):
            self._wait_for_smp_console(vm, pattern)
        vm.shutdown()


if __name__ == '__main__':
    LinuxKernelTest.main()
