K=kernel
B=boot

# the boot skeleton. each conversion step adds the C++ version
# of one part of the C port (tag v0.1-x86_64-c) here.
OBJS = \
  $K/entry.o \
  $K/start.o \
  $K/main.o \
  $K/console.o \
  $K/earlytrap.o \
  $K/earlyvec.o \
  $K/fbcons.o \
  $K/kbd.o \
  $K/pci.o \
  $K/printk.o \
  $K/string.o \
  $K/uart.o

# the kernel is built with the host's x86-64 g++ and binutils;
# the UEFI loader with clang and lld, which can produce the
# PE/COFF files that UEFI runs.
QEMU = qemu-system-x86_64

CC = gcc
CXX = g++
LD = ld
OBJCOPY = objcopy
OBJDUMP = objdump

# Deterministic builds.
DETFLAGS = -ffile-prefix-map=$(CURDIR)=.

# flags shared by C++ and assembly.
COMMONFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb -gdwarf-2
COMMONFLAGS += $(DETFLAGS)
COMMONFLAGS += -m64 -mno-red-zone -mgeneral-regs-only
COMMONFLAGS += -MD
COMMONFLAGS += -ffreestanding -fno-builtin
COMMONFLAGS += -fno-common -nostdlib
COMMONFLAGS += -fno-stack-protector -fno-pie -fno-pic -no-pie
COMMONFLAGS += -fno-asynchronous-unwind-tables -fcf-protection=none
COMMONFLAGS += -I.

ASFLAGS = $(COMMONFLAGS)

# freestanding C++20: no exceptions, RTTI, or standard library,
# and nothing that needs a C++ runtime (thread-safe statics,
# atexit-registered destructors).
CXXFLAGS = $(COMMONFLAGS) -std=c++20
CXXFLAGS += -fno-exceptions -fno-rtti
CXXFLAGS += -fno-threadsafe-statics -fno-use-cxa-atexit
CXXFLAGS += -Wno-main

LDFLAGS = -m elf_x86_64 -z max-page-size=4096 -z noexecstack --no-warn-rwx-segments

$K/kernel: $(OBJS) $K/kernel.ld
	$(LD) $(LDFLAGS) -T $K/kernel.ld -o $K/kernel $(OBJS)
	$(OBJDUMP) -S $K/kernel > $K/kernel.asm
	$(OBJDUMP) -t $K/kernel | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $K/kernel.sym

$K/%.o: $K/%.S
	$(CC) $(ASFLAGS) -c -o $@ $<

$K/%.o: $K/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# the loader always loads fs.img. until the file system comes
# back (with mkfs), give it one empty block.
fs.img:
	dd if=/dev/zero of=$@ bs=1024 count=1 status=none

-include kernel/*.d

# the UEFI loader, EFI/BOOT/BOOTX64.EFI on the boot partition.
$B/BOOTX64.EFI: $B/loader.c $B/efi.h $K/bootinfo.h
	clang -target x86_64-unknown-windows -ffreestanding -fshort-wchar \
		-mno-red-zone -mno-stack-arg-probe -fno-stack-protector \
		-Wall -Werror -O2 -c -o $B/loader.o $B/loader.c
	lld-link -subsystem:efi_application -entry:efi_main -nodefaultlib \
		-out:$@ $B/loader.o

# a FAT32 boot partition (an EFI System Partition) holding the
# loader, the kernel, and fs.img.
esp.img: $B/BOOTX64.EFI $K/kernel fs.img
	rm -f $@
	dd if=/dev/zero of=$@ bs=1M count=64 status=none
	mformat -i $@ -F ::
	mmd -i $@ ::/EFI ::/EFI/BOOT
	mcopy -i $@ $B/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $@ $K/kernel ::/kernel
	mcopy -i $@ fs.img ::/fs.img

# [platform: real PC] a whole disk: a GPT partition table with
# esp.img as its EFI System Partition. real PC firmware boots a USB
# stick only if it looks like this; qemu and VirtualBox boot the
# same image.
# write it to a USB stick with (CAREFUL: this erases /dev/sdX):
#   sudo dd if=usb.img of=/dev/sdX bs=4M conv=fsync
usb.img: esp.img
	rm -f $@
	dd if=/dev/zero of=$@ bs=1M count=66 status=none
	sgdisk -o -n 1:2048:+64M -t 1:ef00 -c 1:"EFI System" $@ >/dev/null
	dd if=esp.img of=$@ bs=1M seek=1 conv=notrunc status=none

clean: 
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	*/*.o */*.d */*.asm */*.sym \
	$K/kernel fs.img esp.img usb.img ovmf_vars.fd $B/*.o $B/BOOTX64.EFI \
	.gdbinit

# try to generate a unique GDB port
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
# QEMU's gdb stub command line changed in 0.11
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)
ifndef CPUS
CPUS := 1
endif

# UEFI firmware for qemu, from Ubuntu's ovmf package.
OVMF_CODE = /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS = /usr/share/OVMF/OVMF_VARS_4M.fd

ovmf_vars.fd:
	cp $(OVMF_VARS) $@

QEMUOPTS = -machine q35 -m 512M -smp $(CPUS) -nographic -no-reboot
QEMUOPTS += -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE)
QEMUOPTS += -drive if=pflash,format=raw,file=ovmf_vars.fd
QEMUOPTS += -drive format=raw,file=usb.img

# [platform: QEMU] make qemu USB=1 adds a USB controller (xHCI)
# with a USB keyboard. (QEMU's PS/2 keyboard is still there too:
# add i8042=off to -machine to type through USB only.)
ifeq ($(USB),1)
QEMUOPTS += -device qemu-xhci,id=xhci
QEMUOPTS += -device usb-kbd,bus=xhci.0
endif

# [platform: QEMU]
qemu: usb.img ovmf_vars.fd
	$(QEMU) $(QEMUOPTS)

# [platform: VirtualBox] boot the same image in a VirtualBox VM (see vbox.sh).
vbox: usb.img
	CPUS=$(CPUS) ./vbox.sh

.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

qemu-gdb: usb.img ovmf_vars.fd .gdbinit
	@echo "*** Now run 'gdb' in another window." 1>&2
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

print-gdbport:
	@echo $(GDBPORT)

.PHONY: fmt
fmt:
	clang-format -i $(wildcard kernel/*.[ch] kernel/*.cpp)
