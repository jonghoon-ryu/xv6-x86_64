        # the UEFI loader (boot/loader.c) copies the kernel to
        # 0x100000 and jumps here on the first CPU, in 64-bit
        # mode, with a pointer to struct bootinfo in %rdi.
        # other CPUs start in entryother.S.
.section .text
.global _entry
_entry:
        cli
        # set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        # %rsp = stack0 + 4096, for CPU 0
        leaq stack0+4096(%rip), %rsp
        # jump to start() in start.c; %rdi still holds bootinfo.
        call start
spin:
        hlt
        jmp spin
