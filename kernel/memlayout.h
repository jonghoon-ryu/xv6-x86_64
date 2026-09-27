// Physical memory layout

// a PC booted by UEFI firmware looks like this:
//
// 00000000 -- low memory: firmware data, and the page
//             where other CPUs start (see entryother.S)
// 00100000 -- the UEFI loader (boot/loader.c) copies the
//             kernel here, then jumps to _entry.
// the rest of RAM is described by the UEFI memory map;
// the loader also places fs.img in RAM below PHYSTOP.
//
// FEC00000 -- IOAPIC (the ACPI MADT has the real address)
// FEE00000 -- local APIC (the ACPI MADT has the real address)

// the kernel uses physical memory thus:
// 00100000 -- entry.S, then kernel text and data
// end -- start of kernel page allocation area
// PHYSTOP -- end RAM used by the kernel

// the first PC serial port, which the kernel uses as its console.
#define COM1 0x3F8

// the kernel is linked to run here.
#define KERNBASE 0x100000L

// the kernel directly maps physical addresses below PHYSTOP,
// and allocates pages from the free memory there that the
// UEFI memory map reports.
#define PHYSTOP (128 * 1024 * 1024L)

// map the trampoline page to the highest address,
// in both user and kernel space.
#define TRAMPOLINE (MAXVA - PGSIZE)

// x86-64 has no RISC-V-style trap vector register: the CPU finds
// trap handlers through the IDT, and the kernel stack for traps from
// user space through the TSS, which the GDT points to. those tables
// must be readable while a user page table is in use, so they live in
// a page mapped just below the trampoline in every page table.
#define CPUTABLES (TRAMPOLINE - PGSIZE)

// map kernel stacks beneath the CPU tables,
// each surrounded by invalid guard pages.
#define KSTACK(p) (CPUTABLES - ((p) + 1) * 2 * PGSIZE)

// User memory layout.
// Address zero first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
//   ...
//   TRAPFRAME (p->trapframe, used by the trampoline)
//   CPUTABLES (the same page as in the kernel)
//   TRAMPOLINE (the same page as in the kernel)
#define TRAPFRAME (CPUTABLES - PGSIZE)
