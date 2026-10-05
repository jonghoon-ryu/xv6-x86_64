#include "types.h"
#include "param.h"
#include "bootinfo.h"

void main();

// entry.S needs one stack per CPU.
extern "C" {
__attribute__((aligned(16))) char stack0[4096 * NCPU];
}

// the loader's description of memory and devices.
struct bootinfo bootinfo;

// constructors of global objects, collected by kernel.ld.
// there is no C++ runtime to run them, so start() does.
extern "C" void (*__init_array_start[])();
extern "C" void (*__init_array_end[])();

// entry.S jumps here in 64-bit mode on stack0, on the first CPU,
// with the loader's struct bootinfo. extern "C" so that entry.S
// can call it by its plain name.
//
// the RISC-V start() switches from machine mode to supervisor
// mode. UEFI has already put the CPU in 64-bit (long) mode at
// privilege level 0, with paging on and all of memory mapped at
// its physical address, so there is less to do here.
extern "C" void
start(struct bootinfo *bi)
{
  // the loader's copy lives in memory the kernel may reuse.
  bootinfo = *bi;

  for (auto ctor = __init_array_start; ctor != __init_array_end; ctor++)
    (*ctor)();

  main();
}
