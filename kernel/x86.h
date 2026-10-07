// x86-64 definitions: this file takes the place of riscv.h.
// Where an x86-64 mechanism corresponds to a RISC-V one, the
// comment names the RISC-V counterpart.

#define MSR_EFER    0xC0000080
#define MSR_GS_BASE 0xC0000101
#define EFER_NXE    (1L << 11) // allow the no-execute PTE bit

// segment selectors; see the GDT built in trap.c.
#define SEG_KCODE 1 // kernel code
#define SEG_KDATA 2 // kernel data+stack
#define SEG_UCODE 3 // user code
#define SEG_UDATA 4 // user data+stack
#define SEG_TSS   5 // this CPU's task state (takes two slots)
#define NSEGS     7

#define KCODE_SEL (SEG_KCODE << 3)
#define KDATA_SEL (SEG_KDATA << 3)
#define UCODE_SEL ((SEG_UCODE << 3) | 3)
#define UDATA_SEL ((SEG_UDATA << 3) | 3)
#define TSS_SEL   (SEG_TSS << 3)

// trap numbers (RISC-V scause values).
#define T_DIVIDE  0  // divide error
#define T_DEBUG   1  // debug exception
#define T_NMI     2  // non-maskable interrupt
#define T_BRKPT   3  // breakpoint
#define T_ILLOP   6  // illegal opcode
#define T_DBLFLT  8  // double fault
#define T_GPFLT   13 // general protection fault
#define T_PGFLT   14 // page fault

#define T_IRQ0       32 // IRQ 0 corresponds to int T_IRQ0
#define IRQ_TIMER    0
#define IRQ_KBD      1
#define IRQ_COM1     4
#define IRQ_ERROR    19
#define IRQ_SPURIOUS 31

#define T_SYSCALL 64 // system call (RISC-V ecall)
#define NIDT      65 // number of IDT entries

#ifndef __ASSEMBLER__

// read and write Model Specific Registers.
static inline uint64
rdmsr(uint32 msr)
{
  uint32 lo, hi;
  asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return ((uint64)hi << 32) | lo;
}

static inline void
wrmsr(uint32 msr, uint64 x)
{
  asm volatile("wrmsr" : : "c"(msr), "a"((uint32)x), "d"((uint32)(x >> 32)));
}

// the kernel keeps each CPU's id in the GS base register,
// the way RISC-V xv6 keeps the hartid in tp.
static inline uint64
r_gsbase()
{
  return rdmsr(MSR_GS_BASE);
}

static inline void
w_gsbase(uint64 x)
{
  wrmsr(MSR_GS_BASE, x);
}

// page table base register (RISC-V satp).
static inline uint64
r_cr3()
{
  uint64 x;
  asm volatile("mov %%cr3, %0" : "=r"(x));
  return x;
}

static inline void
w_cr3(uint64 x)
{
  asm volatile("mov %0, %%cr3" : : "r"(x) : "memory");
}

// faulting virtual address of a page fault (RISC-V stval).
static inline uint64
r_cr2()
{
  uint64 x;
  asm volatile("mov %%cr2, %0" : "=r"(x));
  return x;
}

// RFLAGS: interrupt enable is bit 9 (RISC-V sstatus.SIE).
#define FL_IF 0x200

static inline uint64
r_rflags()
{
  uint64 x;
  asm volatile("pushfq; popq %0" : "=r"(x));
  return x;
}

// enable device interrupts
static inline void
intr_on()
{
  asm volatile("sti");
}

// disable device interrupts
static inline void
intr_off()
{
  asm volatile("cli");
}

// disable device interrupts, returning the old rflags. reading and
// clearing happen together, so no interrupt can arrive in between
// (RISC-V csrrc on sstatus).
static inline uint64
intr_off_save()
{
  uint64 x;
  asm volatile("pushfq; cli; popq %0" : "=r"(x) : : "memory");
  return x;
}

// are device interrupts enabled?
static inline int
intr_get()
{
  return (r_rflags() & FL_IF) != 0;
}

static inline uint64
r_sp()
{
  uint64 x;
  asm volatile("mov %%rsp, %0" : "=r"(x));
  return x;
}

// port I/O (RISC-V devices are all memory-mapped;
// PC serial ports use the separate I/O space).
static inline uchar
inb(ushort port)
{
  uchar data;
  asm volatile("inb %1, %0" : "=a"(data) : "d"(port));
  return data;
}

static inline void
outb(ushort port, uchar data)
{
  asm volatile("outb %0, %1" : : "a"(data), "d"(port));
}

// 32-bit port I/O, for PCI configuration space (pci.cpp).
static inline uint32
inl(ushort port)
{
  uint32 data;
  asm volatile("inl %1, %0" : "=a"(data) : "d"(port));
  return data;
}

static inline void
outl(ushort port, uint32 data)
{
  asm volatile("outl %0, %1" : : "a"(data), "d"(port));
}

// the pseudo-descriptor used by lgdt and lidt.
struct dtr {
  ushort limit;
  uint64 base;
} __attribute__((packed));

static inline void
lidt(void *base, int size)
{
  struct dtr d = { (ushort)(size - 1), (uint64)base };
  asm volatile("lidt %0" : : "m"(d));
}

static inline void
lgdt(void *base, int size)
{
  struct dtr d = { (ushort)(size - 1), (uint64)base };
  asm volatile("lgdt %0" : : "m"(d));
}

static inline void
ltr(ushort sel)
{
  asm volatile("ltr %0" : : "r"(sel));
}

// flush the TLB: reloading cr3 discards all non-global entries
// (RISC-V sfence.vma).
static inline void
sfence_vma()
{
  w_cr3(r_cr3());
}

// an IDT entry: where the CPU goes for one trap vector.
struct gatedesc {
  ushort off_15_0;  // low bits of handler address
  ushort cs;        // code segment selector
  uchar ist;        // interrupt stack table index; unused
  uchar type_attr;  // present, privilege level, gate type
  ushort off_31_16;
  uint off_63_32;
  uint rsvd;
};

// the task state segment. x86-64 no longer switches tasks with it;
// xv6 uses it only for rsp0, the stack the CPU switches to on a
// trap from user space.
struct taskstate {
  uint rsvd0;
  uint64 rsp0;
  uint64 rsp1;
  uint64 rsp2;
  uint64 rsvd1;
  uint64 ist[7];
  uint64 rsvd2;
  ushort rsvd3;
  ushort iomb; // I/O permission bitmap offset
} __attribute__((packed));

typedef uint64 pte_t;
typedef uint64 *pagetable_t; // 512 PTEs

#endif // __ASSEMBLER__


// page fault error code bits
#define PF_WRITE 0x2  // fault was a write
#define PF_USER  0x4  // fault was in user mode
#define PF_INSTR 0x10 // fault was an instruction fetch

#define PGSIZE  4096 // bytes per page
#define PGSHIFT 12   // bits of offset within a page

#define PGROUNDUP(sz)  (((sz) + PGSIZE - 1) & ~(PGSIZE - 1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE - 1))

// x86-64 PTE bits. PTE_R and PTE_X are software bits kept so that the
// RISC-V code's permission arguments still make sense: every present
// page is readable on x86, and mappages() turns a missing PTE_X into
// the hardware no-execute bit.
#define PTE_V   (1L << 0) // present
#define PTE_W   (1L << 1) // writeable
#define PTE_U   (1L << 2) // user can access
#define PTE_PWT (1L << 3) // write-through
#define PTE_PCD (1L << 4) // cache disable, for device registers
#define PTE_R   (1L << 9)  // software: readable
#define PTE_X   (1L << 10) // software: executable
#define PTE_NX  (1L << 63) // hardware: no execute

// a physical address is stored in place in the PTE.
#define PA2PTE(pa) ((uint64)(pa))

#define PTE2PA(pte) ((pte) & 0x000FFFFFFFFFF000L)

#define PTE_FLAGS(pte) ((pte) & 0x7FF)

// extract the four 9-bit page table indices from a virtual address.
#define PXMASK         0x1FF // 9 bits
#define PXSHIFT(level) (PGSHIFT + (9 * (level)))
#define PX(level, va)  ((((uint64)(va)) >> PXSHIFT(level)) & PXMASK)

// the page table base value for a page table page (RISC-V MAKE_SATP).
#define MAKE_CR3(pagetable) ((uint64)(pagetable))

// one beyond the highest possible user virtual address.
// x86-64 allows 47 bits, but xv6 keeps RISC-V's limit
// so that the user address space layout is unchanged.
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))
