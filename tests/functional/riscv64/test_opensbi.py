#!/usr/bin/env python3
#
# OpenSBI boot test for RISC-V machines
#
# Copyright (c) 2022, Ventana Micro
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import QemuSystemTest
from qemu_test import wait_for_console_pattern

class RiscvOpenSBI(QemuSystemTest):

    timeout = 5

    def boot_opensbi(self):
        self.vm.set_console()
        self.vm.launch()
        wait_for_console_pattern(self, 'Platform Name')
        wait_for_console_pattern(self, 'Boot HART MEDELEG')

    def test_riscv_spike(self):
        self.set_machine('spike')
        self.boot_opensbi()

    def test_riscv_beaglev_ahead(self):
        self.set_machine('beaglev-ahead')
        self.vm.set_console()
        self.vm.launch()
        wait_for_console_pattern(self, 'Platform Name               : BeagleV Ahead')
        wait_for_console_pattern(self, 'Platform HART Count         : 4')
        wait_for_console_pattern(self,
                                 'Platform Timer Device       : aclint-mtimer @ 3000000Hz')
        wait_for_console_pattern(self, 'Platform Console Device     : uart8250')
        wait_for_console_pattern(self,
                                 'Platform PMU Device         : thead,c900-pmu')
        wait_for_console_pattern(self, 'Boot HART Priv Version      : v1.11')
        wait_for_console_pattern(self,
                                 'Boot HART ISA Extensions    : zicntr,zihpm')
        wait_for_console_pattern(self, 'Boot HART PMP Count         : 0')
        wait_for_console_pattern(self, 'Boot HART PMP Address Bits  : 0')
        wait_for_console_pattern(self, 'Boot HART MHPM Info         : 16 (0x0007fff8)')

    def test_riscv_sifive_u(self):
        self.set_machine('sifive_u')
        self.boot_opensbi()

    def test_riscv_tt_atlantis(self):
        self.set_machine('tt-atlantis')
        self.boot_opensbi()

    def test_riscv_virt(self):
        self.set_machine('virt')
        self.boot_opensbi()

if __name__ == '__main__':
    QemuSystemTest.main()
