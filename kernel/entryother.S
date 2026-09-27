#include "x86.h"

# Each non-boot CPU ("AP") is started up in response to a STARTUP
# IPI from the boot CPU (see startothers() in main.c and
# lapicstartap() in lapic.c). The AP begins in 16-bit real mode at
# the start of a page below 1MB: the page the UEFI loader set aside
# (bootinfo.apboot). startothers() copies this code there and fills
# in the addresses it needs; this code is never run in place.
#
# qemu starts all RISC-V harts at once, already able to run the
# kernel; a PC's other CPUs instead start the way PCs did in 1981,
# so this code must step from 16-bit to 32-bit to 64-bit mode,
# turning on paging with the kernel's page table on the way.
#
# the code must work at whatever address it is copied to, so it
# refers to its own data relative to %ebx, the page's address.

#define OFF(x) ((x) - entryother_start)

.section .rodata
.globl entryother_start
.globl entryother_end

.code16
entryother_start:
        cli
        cld

        # %ds = %cs, so that 16-bit data references work;
        # %ebx = linear address of this page.
        movw %cs, %ax
        movw %ax, %ds
        xorl %ebx, %ebx
        movw %ax, %bx
        shll $4, %ebx

        # switch to 32-bit protected mode, with caching on
        # (INIT leaves the cache-disable bits set).
        lgdtl OFF(eo_gdtdesc)
        movl %cr0, %eax
        andl $~0x60000000, %eax // clear CD, NW
        orl $0x1, %eax          // PE
        movl %eax, %cr0
        ljmpl *OFF(eo_far32)

.code32
eo_start32:
        movw $0x10, %ax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss

        # enable PAE, which 64-bit paging requires.
        movl %cr4, %eax
        orl $0x20, %eax
        movl %eax, %cr4

        # the kernel's page table; it maps this page at its own address.
        movl OFF(eo_cr3)(%ebx), %eax
        movl %eax, %cr3

        # ask for 64-bit mode and no-execute PTEs.
        movl $MSR_EFER, %ecx
        rdmsr
        orl $0x900, %eax // LME, NXE
        wrmsr

        # turn on paging, which also turns on 64-bit mode.
        movl %cr0, %eax
        orl $0x80000000, %eax // PG
        movl %eax, %cr0

        # enter 64-bit code.
        ljmp *OFF(eo_far64)(%ebx)

.code64
eo_start64:
        movq OFF(eo_stack)(%rbx), %rsp
        movq OFF(eo_id)(%rbx), %rdi
        movq OFF(eo_entry)(%rbx), %rax
        call *%rax // mpenter(id), in start.c
1:
        hlt
        jmp 1b

        # a temporary GDT, just enough to reach 64-bit mode.
        # the kernel's own GDT (trap.c) replaces it.
.p2align 4
.globl eo_gdt
eo_gdt:
        .quad 0                  // null
        .quad 0x00CF9A000000FFFF // 0x08: 32-bit code
        .quad 0x00CF92000000FFFF // 0x10: data
        .quad 0x00209A0000000000 // 0x18: 64-bit code

.globl eo_gdtdesc
eo_gdtdesc:
        .word 4 * 8 - 1
        .long 0 // filled in: address of eo_gdt

.globl eo_far32
eo_far32:
        .long 0 // filled in: address of eo_start32
        .word 0x08

.globl eo_far64
eo_far64:
        .long 0 // filled in: address of eo_start64
        .word 0x18

.p2align 3
.globl eo_cr3, eo_stack, eo_id, eo_entry, eo_start32, eo_start64
eo_cr3:   .quad 0 // kernel page table
eo_stack: .quad 0 // top of this CPU's stack in stack0
eo_id:    .quad 0 // this CPU's id
eo_entry: .quad 0 // mpenter()
entryother_end:
