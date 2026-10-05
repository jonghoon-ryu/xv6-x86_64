#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "bootinfo.h"
#include "defs.h"

extern struct bootinfo bootinfo;

// the boot skeleton, growing one conversion step at a time.
// step 4: printk() output goes to both the serial port and the
// screen (fbcons.cpp), as in the C version. the hand-drawn "xv6"
// letters of steps 1-3 are gone.

// UEFI memory types (the UEFI spec's EFI_MEMORY_TYPE), for the
// memory map summary below.
static const char *
memtype(uint t)
{
  static const char *names[] = {
    "reserved", "loader code", "loader data", "boot services code",
    "boot services data", "runtime code", "runtime data", "free",
    "unusable", "ACPI tables", "ACPI NVS", "MMIO", "MMIO port", "PAL code",
    "persistent",
  };
  return t < sizeof(names) / sizeof(names[0]) ? names[t] : "other";
}

// what the loader handed over, and a summary of the UEFI memory map:
// how many pages of each type. kalloc.cpp will use the free ones.
static void
printbootinfo()
{
  // bootinfo's fields are unsigned long long (see bootinfo.h);
  // the casts match them to %p and %lu, which g++ checks.
  printk("screen: %dx%d, frame buffer at %p\n", (int)bootinfo.fb_width,
         (int)bootinfo.fb_height, (void *)bootinfo.fb_base);
  printk("ACPI root pointer at %p\n", (void *)bootinfo.rsdp);
  printk("fs.img: %lu bytes at %p\n", (uint64)bootinfo.fsimg_size,
         (void *)bootinfo.fsimg);

  uint64 pages[16] = {};
  int n = 0;
  for (uint64 off = 0; off < bootinfo.memmap_size;
       off += bootinfo.memmap_descsize, n++) {
    auto d = reinterpret_cast<efi_memdesc *>(bootinfo.memmap + off);
    pages[d->type < 15 ? d->type : 15] += d->npages;
  }
  printk("UEFI memory map: %d entries\n", n);
  for (uint t = 0; t < 16; t++)
    if (pages[t] != 0)
      printk("  %s: %ld pages (%ld MB)\n", memtype(t), pages[t],
             pages[t] * 4096 / (1024 * 1024));
}

// start() jumps here on the first CPU.
void
main()
{
  consoleinit();
  printk("\n");
  printk("xv6 kernel is booting (C++, step 4)\n");
  printk("\n");
  printbootinfo();
  printk("nothing else yet; halting\n");

  for (;;)
    asm volatile("hlt");
}
