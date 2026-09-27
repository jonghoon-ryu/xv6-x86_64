#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[];

// entry stubs, one per vector, in trampoline.S and kernelvec.S.
extern uint64 uvectors[], kvectors[];

// the IDT for traps from kernel code. it plays the part of RISC-V's
// stvec = kernelvec; the IDT in cputables plays stvec = uservec.
static struct gatedesc kidt[NIDT];

// IDT, GDT, and TSS, mapped at CPUTABLES in every page table.
__attribute__((aligned(PGSIZE))) struct cputables cputables;
_Static_assert(sizeof(struct cputables) <= PGSIZE, "cputables too big");

extern int devintr(uint64 trapno);

// fill in an IDT entry: an interrupt gate, so that the CPU turns
// off interrupts on entry, as RISC-V does on a trap.
static void
setgate(struct gatedesc *g, uint64 handler, int dpl)
{
  g->off_15_0 = handler & 0xffff;
  g->cs = KCODE_SEL;
  g->ist = 0;
  g->type_attr = 0x80 | (dpl << 5) | 0xE; // present, dpl, 64-bit interrupt gate
  g->off_31_16 = (handler >> 16) & 0xffff;
  g->off_63_32 = handler >> 32;
  g->rsvd = 0;
}

void
trapinit(void)
{
  initlock(&tickslock, "time");

  for (int i = 0; i < NIDT; i++) {
    setgate(&kidt[i], kvectors[i], 0);
    // user code may use int $T_SYSCALL, but no other vector.
    uint64 stub = TRAMPOLINE + (uvectors[i] - (uint64)trampoline);
    setgate(&cputables.uidt[i], stub, i == T_SYSCALL ? 3 : 0);
  }
}

// set up this CPU's segments, task state, and kernel trap vector.
// x86-64 still requires a GDT and a TSS, though it hardly uses
// segments; RISC-V has nothing like them.
void
trapinithart(void)
{
  int id = cpuid();
  struct taskstate *ts = &cputables.cpu[id].ts;
  uint64 *gdt = cputables.cpu[id].gdt;

  // every descriptor is marked accessed, so that the CPU need never
  // write to these tables while a user page table is in use.
  gdt[0] = 0;
  gdt[SEG_KCODE] = 0x00209B0000000000L; // 64-bit, present, dpl 0, code
  gdt[SEG_KDATA] = 0x00CF93000000FFFFL; // present, dpl 0, data
  gdt[SEG_UCODE] = 0x0020FB0000000000L; // 64-bit, present, dpl 3, code
  gdt[SEG_UDATA] = 0x00CFF3000000FFFFL; // present, dpl 3, data

  // on a trap from user space, the CPU pushes the trap frame onto
  // the stack that ts->rsp0 points to: the end of p->trapframe,
  // which is at the same address (TRAPFRAME) in every process.
  memset(ts, 0, sizeof(*ts));
  ts->rsp0 = TRAPFRAME + sizeof(struct trapframe);
  ts->iomb = sizeof(*ts); // no I/O permission bitmap

  uint64 tsva = CPUTABLES + ((uint64)ts - (uint64)&cputables);
  uint64 limit = sizeof(*ts) - 1;
  gdt[SEG_TSS] = (limit & 0xffff) | ((tsva & 0xffffff) << 16) |
                 (0x89L << 40) | // present, 64-bit available TSS
                 (((tsva >> 24) & 0xff) << 56);
  gdt[SEG_TSS + 1] = tsva >> 32;

  uint64 gdtva = CPUTABLES + ((uint64)gdt - (uint64)&cputables);
  lgdt((void *)gdtva, NSEGS * sizeof(uint64));

  // reload the segment registers from the new GDT.
  // cs can only be changed by a far jump or return.
  asm volatile("pushq %0\n"
               "leaq 1f(%%rip), %%rax\n"
               "pushq %%rax\n"
               "lretq\n"
               "1:\n"
               :
               : "i"(KCODE_SEL)
               : "rax", "memory");
  asm volatile("movw %0, %%ds\n"
               "movw %0, %%es\n"
               "movw %0, %%ss\n"
               :
               : "r"((ushort)KDATA_SEL));
  asm volatile("movw %0, %%fs\n"
               "movw %0, %%gs\n"
               :
               : "r"((ushort)0));
  ltr(TSS_SEL);

  // loading gs may have cleared its base; put the cpu id back.
  w_gsbase(id);

  // send traps to kernelvec (RISC-V: w_stvec(kernelvec)).
  lidt(kidt, sizeof(kidt));
}

//
// handle an interrupt, exception, or system call from user space.
// called from, and returns to, trampoline.S
// return value is user cr3 for trampoline.S to switch to.
//
uint64
usertrap(void)
{
  int which_dev = 0;

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  lidt(kidt, sizeof(kidt)); //DOC: kernelvec

  struct proc *p = myproc();

  if ((p->trapframe->cs & 3) != 3)
    panic("usertrap: not from user mode");

  uint64 trapno = p->trapframe->trapno;

  if (trapno == T_SYSCALL) {
    // system call

    if (killed(p))
      kexit(-1);

    // unlike RISC-V's ecall, int $T_SYSCALL leaves the saved rip
    // pointing at the next instruction, so no adjustment is needed.

    // an interrupt would overwrite cr2,
    // so enable only now that we're done with it.
    intr_on();

    syscall();
  } else if ((which_dev = devintr(trapno)) != 0) {
    // ok
  } else if (trapno == T_PGFLT && (p->trapframe->err & PF_INSTR) == 0 &&
             vmfault(p->pagetable, p->sz, r_cr2(),
                     (p->trapframe->err & PF_WRITE) ? 0 : 1) != 0) {
    // page fault on lazily-allocated page
  } else {
    printk("usertrap(): unexpected trap %ld err 0x%lx pid=%d\n", trapno,
           p->trapframe->err, p->pid);
    printk("            rip=0x%lx cr2=0x%lx\n", p->trapframe->rip, r_cr2());
    setkilled(p);
  }

  if (killed(p))
    kexit(-1);

  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2)
    yield();

  prepare_return();

  // the user page table to switch to, for trampoline.S
  uint64 cr3 = MAKE_CR3(p->pagetable);

  // return to trampoline.S; cr3 value in %rax.
  return cr3;
}

//
// set up trapframe and control registers for a return to user space
//
void
prepare_return(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(). because a trap from kernel
  // code to usertrap would be a disaster, turn off interrupts.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  lidt((void *)CPUTABLES, sizeof(cputables.uidt));

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_cr3 = r_cr3();           // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_gsbase(); // cpu id for cpuid()

  // set up the rest of the frame that trampoline.S's iretq will
  // use to get to user space: user mode segments, and interrupts
  // enabled (RISC-V: clear SSTATUS_SPP, set SSTATUS_SPIE).
  p->trapframe->cs = UCODE_SEL;
  p->trapframe->ss = UDATA_SEL;
  p->trapframe->rflags |= FL_IF;
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void
kerneltrap(uint64 trapno, uint64 err, uint64 rip)
{
  int which_dev = 0;

  if (intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if ((which_dev = devintr(trapno)) == 0) {
    // interrupt or trap from an unknown source
    printk("trap %ld err 0x%lx rip=0x%lx cr2=0x%lx\n", trapno, err, rip,
           r_cr2());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2 && myproc() != 0)
    yield();

  // unlike RISC-V, there are no trap registers to restore:
  // kernelvec's iretq uses the frame saved on this stack.
}

void
clockintr()
{
  if (cpuid() == 0) {
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
  }

  // the local APIC timer runs in periodic mode (lapic.c),
  // so there is no need to ask for the next timer interrupt.
}

// check if it's a device interrupt, and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr(uint64 trapno)
{
  if (trapno == T_IRQ0 + IRQ_TIMER) {
    clockintr();
    // tell the local APIC the interrupt is handled,
    // so that it can deliver the next one.
    lapiceoi();
    return 2;
  } else if (trapno == T_IRQ0 + IRQ_COM1) {
    uartintr();
    lapiceoi();
    return 1;
  } else if (trapno == T_IRQ0 + IRQ_ERROR) {
    printk("local APIC error\n");
    lapiceoi();
    return 1;
  } else if (trapno == T_IRQ0 + IRQ_SPURIOUS) {
    // spurious interrupts need no end-of-interrupt.
    return 1;
  } else {
    return 0;
  }
}
