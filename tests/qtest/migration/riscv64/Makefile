# To specify cross compiler prefix, use CROSS_PREFIX=
#   make CROSS_PREFIX=riscv64-linux-gnu-

.PHONY: all clean
all: c910-pmu-guest.h riscv-pmu-guest.h

%-pmu-guest.h: %-pmu-guest.bin
	echo "$$__note" > $@
	xxd -i $< | sed -e '/.*int.*/d' >> $@

%-pmu-guest.bin: %-pmu-guest.elf
	$(CROSS_PREFIX)objcopy -O binary $< $@

%-pmu-guest.elf: %-pmu-guest.S pmu-migration-guest.S
	$(CROSS_PREFIX)gcc -o $@ -nostdlib -static -Wl,--build-id=none \
		-Wl,-Ttext=0x100000 $<

clean:
	$(RM) *-pmu-guest.bin *-pmu-guest.elf *-pmu-guest.h
