#!/usr/bin/env python3
#
# Functional test that boots Linux from the BeagleV Ahead eMMC model
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset, LinuxKernelTest, wait_for_console_pattern


class BeagleVAhead(LinuxKernelTest):

    ASSET_KERNEL = Asset(
        'https://storage.tuxboot.com/kernels/6.11.9/riscv64/Image',
        '174f8bb87f08961e54fa3fcd954a8e31f4645f6d6af4dd43983d5e9841490fb0')
    ASSET_ROOTFS = Asset(
        ('https://github.com/groeck/linux-build-test/raw/'
         '9819da19e6eef291686fdd7b029ea00e764dc62f/rootfs/riscv64/'
         'rootfs.ext2.gz'),
        'b6ed95610310b7956f9bf20c4c9c0c05fea647900df441da9dfe767d24e8b28b')

    def _boot_emmc(self, name, kernel_path, rootfs_path, init_script):
        vm = self.get_vm(name=name)
        vm.set_console()
        # This kernel leaves the runtime UART deferred for an unresolved
        # reason.  The init script therefore reports through /dev/kmsg, which
        # earlycon exposes to the test.
        #
        # maxcpus=1 deliberately keeps secondary-hart bring-up outside this
        # storage-specific regression test.  SMP needs a separate Linux gate.
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200 earlycon maxcpus=1 '
                               'root=/dev/mmcblk1 rootwait rw '
                               'panic=-1 init=/bin/sh -- -c ' +
                               init_script)
        vm.add_args('-kernel', kernel_path,
                    '-append', kernel_command_line,
                    '-drive', f'file={rootfs_path},if=sd,index=0,format=raw',
                    '-no-reboot')

        vm.launch()
        self.wait_for_console_pattern('mmc1: new HS400 MMC card', vm=vm)
        self.wait_for_console_pattern('VFS: Mounted root (ext2 filesystem)',
                                      vm=vm)
        return vm

    def test_emmc_root(self):
        self.set_machine('beaglev-ahead')
        kernel_path = self.ASSET_KERNEL.fetch()
        rootfs_path = self.uncompress(self.ASSET_ROOTFS)
        expected_hash = ('fb5ac1fab9c5b0e2b328bbe2149dee4664cf064618884f78c'
                         '10e5c3bf6a05cda')

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
        wait_for_console_pattern(self, expected_hash,
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
        wait_for_console_pattern(self, expected_hash,
                                 failure_message='EMMC_PROCESS_REOPEN_FAIL',
                                 vm=vm)
        wait_for_console_pattern(self, 'EMMC_PROCESS_REOPEN_PASS',
                                 failure_message='EMMC_PROCESS_REOPEN_FAIL',
                                 vm=vm)
        vm.shutdown()


if __name__ == '__main__':
    LinuxKernelTest.main()
