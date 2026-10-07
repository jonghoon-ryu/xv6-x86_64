//
// [platform: real PC] report CPU exceptions on the screen, until
// the traps part of the plan brings back trap.cpp and its IDT.
// with no IDT at all, an exception becomes a triple fault: QEMU
// stops or resets, and a real PC reboots without a word. this
// makes a failure on a real PC something a photo can show.
//
// not in the C version.
//

#include "types.h"
#include "param.h"
#include "x86.h"
#include "defs.h"

extern "C" char earlyvectors[]; // earlyvec.S: 32 stubs, 16 bytes apart

static struct gatedesc idt[32];

static const char *
excname(uint64 v)
{
  static const char *names[] = {
    "divide error", "debug", "NMI", "breakpoint", "overflow", "bound",
    "invalid opcode", "no FPU", "double fault", "", "invalid TSS",
    "segment not present", "stack fault", "general protection",
    "page fault", "", "FPU error", "alignment check", "machine check",
    "SIMD error",
  };
  return v < sizeof(names) / sizeof(names[0]) ? names[v] : "?";
}

void
earlytrapinit()
{
  ushort cs;
  asm volatile("mov %%cs, %0" : "=r"(cs));
  for (int i = 0; i < 32; i++) {
    uint64 a = (uint64)earlyvectors + 16 * i;
    idt[i].off_15_0 = a & 0xFFFF;
    idt[i].cs = cs;
    idt[i].ist = 0;
    idt[i].type_attr = 0x8E; // present, kernel only, interrupt gate
    idt[i].off_31_16 = (a >> 16) & 0xFFFF;
    idt[i].off_63_32 = a >> 32;
    idt[i].rsvd = 0;
  }
  lidt(idt, sizeof(idt));
}

// f: vector, error code, then what the CPU pushed: rip, cs, rflags.
extern "C" void
earlytrap(uint64 *f)
{
  printk("\nCPU exception %ld (%s), error code %lx\n", f[0], excname(f[0]), f[1]);
  printk("  rip %p", (void *)f[2]);
  if (f[0] == 14)
    printk(", address %p (cr2)", (void *)r_cr2());
  printk("\n");
  panic("early trap: please take a photo of this screen");
}
