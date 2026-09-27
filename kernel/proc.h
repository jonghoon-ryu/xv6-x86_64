// Saved registers for kernel context switches.
struct context {
  uint64 ra; // where swtch() returns to
  uint64 sp;

  // callee-saved (x86-64 System V ABI)
  uint64 rbx;
  uint64 rbp;
  uint64 r12;
  uint64 r13;
  uint64 r14;
  uint64 r15;
};

// Per-CPU state.
struct cpu {
  struct proc *proc;      // The process running on this cpu, or null.
  struct context context; // swtch() here to enter scheduler().
  int noff;               // Depth of push_off() nesting.
  int intena;             // Were interrupts enabled before push_off()?
};

extern struct cpu cpus[NCPU];

// the tables the CPU consults on a trap from user space, while the
// user page table is still in use. trap.c keeps them in one page,
// mapped at CPUTABLES in every page table (see memlayout.h).
struct cputables {
  struct gatedesc uidt[NIDT]; // IDT for traps from user space
  struct {
    uint64 gdt[NSEGS]; // this CPU's segment descriptors
    struct taskstate ts;
  } __attribute__((aligned(16))) cpu[NCPU];
};

extern struct cputables cputables;

// per-process data for the trap handling code in trampoline.S.
// sits in a page by itself just under the CPU tables page in the
// user page table. not specially mapped in the kernel page table.
//
// on a trap from user space, the CPU switches to the stack whose
// top the TSS names, which is the end of this structure at
// TRAPFRAME. so the CPU itself pushes ss..rip (and maybe err),
// then uservec pushes trapno (via the vector stub) and the
// general-purpose registers, filling in the structure from the
// bottom up. uservec then initializes registers from the
// trapframe's kernel_sp, kernel_hartid, kernel_cr3, and jumps to
// kernel_trap. prepare_return() and userret in trampoline.S set up
// the trapframe's kernel_*, restore user registers from the
// trapframe, switch to the user page table, and enter user space
// with iretq.
struct trapframe {
  /*   0 */ uint64 kernel_cr3;    // kernel page table
  /*   8 */ uint64 kernel_sp;     // top of process's kernel stack
  /*  16 */ uint64 kernel_trap;   // usertrap()
  /*  24 */ uint64 kernel_hartid; // saved kernel GS base (the cpu id)
  /*  32 */ uint64 r15;
  /*  40 */ uint64 r14;
  /*  48 */ uint64 r13;
  /*  56 */ uint64 r12;
  /*  64 */ uint64 r11;
  /*  72 */ uint64 r10;
  /*  80 */ uint64 r9;
  /*  88 */ uint64 r8;
  /*  96 */ uint64 rdi;
  /* 104 */ uint64 rsi;
  /* 112 */ uint64 rbp;
  /* 120 */ uint64 rdx;
  /* 128 */ uint64 rcx;
  /* 136 */ uint64 rbx;
  /* 144 */ uint64 rax;
  /* 152 */ uint64 trapno; // pushed by the vector stub
  /* 160 */ uint64 err;    // pushed by the CPU, or 0 by the stub
  /* 168 */ uint64 rip;    // user program counter (RISC-V epc)
  /* 176 */ uint64 cs;
  /* 184 */ uint64 rflags;
  /* 192 */ uint64 rsp;    // user stack pointer
  /* 200 */ uint64 ss;
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state; // Process state
  void *chan;           // If non-zero, sleeping on chan
  int killed;           // If non-zero, have been killed
  int xstate;           // Exit status to be returned to parent's wait
  int pid;              // Process ID

  // wait_lock must be held when using this:
  struct proc *parent; // Parent process

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
};
