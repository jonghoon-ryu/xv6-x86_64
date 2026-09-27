#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "bootinfo.h"
#include "defs.h"

void main();

// entry.S needs one stack per CPU.
__attribute__((aligned(16))) char stack0[4096 * NCPU];

// the loader's description of memory and devices.
struct bootinfo bootinfo;

// entry.S jumps here in 64-bit supervisor mode on stack0,
// on the first CPU only.
//
// the RISC-V start() switches from machine mode to supervisor
// mode. UEFI has already put the CPU in 64-bit (long) mode at
// privilege level 0, with paging on and all of memory mapped at
// its physical address, so there is less to do here.
void
start(struct bootinfo *bi)
{
  // the loader's copy lives in memory that kinit() will not
  // free, but keep a copy in the kernel anyway.
  bootinfo = *bi;

  // allow page table entries to forbid execution.
  wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_NXE);

  // keep each CPU's id in its GS base register, for cpuid().
  w_gsbase(0);

  main();
}

// set by mpenter() so that startothers() knows the CPU is running.
volatile int apstarted;

// entryother.S jumps here on each of the other CPUs, in 64-bit
// mode, on its part of stack0, with the kernel's page table.
void
mpenter(uint64 id)
{
  w_gsbase(id);
  __atomic_store_n(&apstarted, 1, __ATOMIC_RELEASE);
  main();
}
