K=kernel
U=user
B=boot

OBJS = \
  $K/entry.o \
  $K/start.o \
  $K/console.o \
  $K/printk.o \
  $K/uart.o \
  $K/kalloc.o \
  $K/spinlock.o \
  $K/string.o \
  $K/main.o \
  $K/vm.o \
  $K/proc.o \
  $K/swtch.o \
  $K/trampoline.o \
  $K/trap.o \
  $K/syscall.o \
  $K/sysproc.o \
  $K/bio.o \
  $K/fs.o \
  $K/log.o \
  $K/sleeplock.o \
  $K/file.o \
  $K/pipe.o \
  $K/exec.o \
  $K/sysfile.o \
  $K/kernelvec.o \
  $K/acpi.o \
  $K/lapic.o \
  $K/ioapic.o \
  $K/ramdisk.o

# the kernel and user programs are built with the host's
# x86-64 gcc and binutils; the UEFI loader with clang and lld,
# which can produce the PE/COFF files that UEFI runs.
QEMU = qemu-system-x86_64

CC = gcc
LD = ld
OBJCOPY = objcopy
OBJDUMP = objdump

# Deterministic builds.
DETFLAGS = -ffile-prefix-map=$(CURDIR)=.

CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb -gdwarf-2
CFLAGS += $(DETFLAGS)
CFLAGS += -m64 -mno-red-zone -mgeneral-regs-only
CFLAGS += -std=gnu99
CFLAGS += -MD
CFLAGS += -ffreestanding
CFLAGS += -fno-common -nostdlib
CFLAGS += -fno-builtin-strncpy -fno-builtin-strncmp -fno-builtin-strlen -fno-builtin-memset
CFLAGS += -fno-builtin-memmove -fno-builtin-memcmp -fno-builtin-log -fno-builtin-bzero
CFLAGS += -fno-builtin-strchr -fno-builtin-exit -fno-builtin-malloc -fno-builtin-putc
CFLAGS += -fno-builtin-free
CFLAGS += -fno-builtin-memcpy -Wno-main
CFLAGS += -fno-builtin-printf -fno-builtin-fprintf -fno-builtin-vprintf
CFLAGS += -fno-stack-protector -fno-pie -fno-pic -no-pie
CFLAGS += -fno-asynchronous-unwind-tables -fcf-protection=none
CFLAGS += -I.

LDFLAGS = -m elf_x86_64 -z max-page-size=4096 -z noexecstack --no-warn-rwx-segments

$K/kernel: $(OBJS) $K/kernel.ld
	$(LD) $(LDFLAGS) -T $K/kernel.ld -o $K/kernel $(OBJS) 
	$(OBJDUMP) -S $K/kernel > $K/kernel.asm
	$(OBJDUMP) -t $K/kernel | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $K/kernel.sym

$K/%.o: $K/%.S
	$(CC) $(CFLAGS) -c -o $@ $<

tags: $(OBJS)
	etags kernel/*.S kernel/*.c

ULIB = $U/ulib.o $U/usys.o $U/printf.o $U/umalloc.o

_%: %.o $(ULIB) $U/user.ld
	$(LD) $(LDFLAGS) -T $U/user.ld -o $@ $< $(ULIB)
	$(OBJDUMP) -S $@ > $*.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $*.sym

$U/usys.S : $U/usys.pl
	perl $U/usys.pl > $U/usys.S

$U/usys.o : $U/usys.S
	$(CC) $(CFLAGS) -c -o $U/usys.o $U/usys.S

$U/_forktest: $U/forktest.o $(ULIB)
	# forktest has less library code linked in - needs to be small
	# in order to be able to max out the proc table.
	$(LD) $(LDFLAGS) -N -e start -Ttext 0 -o $U/_forktest $U/forktest.o $U/ulib.o $U/usys.o
	$(OBJDUMP) -S $U/_forktest > $U/forktest.asm

mkfs/mkfs: mkfs/mkfs.c $K/fs.h $K/param.h
	gcc -Wno-unknown-attributes -I. -o mkfs/mkfs mkfs/mkfs.c

# Prevent deletion of intermediate files, e.g. cat.o, after first build, so
# that disk image changes after first build are persistent until clean.  More
# details:
# http://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
.PRECIOUS: %.o

UPROGS=\
	$U/_cat\
	$U/_echo\
	$U/_forktest\
	$U/_grep\
	$U/_init\
	$U/_kill\
	$U/_ln\
	$U/_ls\
	$U/_mkdir\
	$U/_rm\
	$U/_sh\
	$U/_stressfs\
	$U/_usertests\
	$U/_grind\
	$U/_wc\
	$U/_zombie\
	$U/_logstress\
	$U/_forphan\
	$U/_dorphan\
	$U/_sync\

fs.img: mkfs/mkfs README $(UPROGS)
	mkfs/mkfs fs.img README $(UPROGS)

-include kernel/*.d user/*.d

# the UEFI loader, EFI/BOOT/BOOTX64.EFI on the boot partition.
$B/BOOTX64.EFI: $B/loader.c $B/efi.h $K/bootinfo.h
	clang -target x86_64-unknown-windows -ffreestanding -fshort-wchar \
		-mno-red-zone -mno-stack-arg-probe -fno-stack-protector \
		-Wall -Werror -O2 -c -o $B/loader.o $B/loader.c
	lld-link -subsystem:efi_application -entry:efi_main -nodefaultlib \
		-out:$@ $B/loader.o

# a FAT boot partition holding the loader, the kernel, and fs.img.
# it boots the same way under qemu, VirtualBox, and a real PC
# (written to a USB stick).
esp.img: $B/BOOTX64.EFI $K/kernel fs.img
	rm -f $@
	dd if=/dev/zero of=$@ bs=1M count=64 status=none
	mformat -i $@ ::
	mmd -i $@ ::/EFI ::/EFI/BOOT
	mcopy -i $@ $B/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $@ $K/kernel ::/kernel
	mcopy -i $@ fs.img ::/fs.img

clean: 
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	*/*.o */*.d */*.asm */*.sym \
	$K/kernel fs.img esp.img ovmf_vars.fd $B/*.o $B/BOOTX64.EFI \
	mkfs/mkfs .gdbinit \
        $U/usys.S \
	$(UPROGS)

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
QEMUOPTS += -drive format=raw,file=esp.img

qemu: esp.img ovmf_vars.fd
	$(QEMU) $(QEMUOPTS)

.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

qemu-gdb: esp.img ovmf_vars.fd .gdbinit
	@echo "*** Now run 'gdb' in another window." 1>&2
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

print-gdbport:
	@echo $(GDBPORT)

.PHONY: fmt
fmt:
	clang-format -i $(wildcard kernel/*.[ch] user/*.[ch] mkfs/*.c)
