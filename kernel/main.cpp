#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "bootinfo.h"
#include "defs.h"

extern struct bootinfo bootinfo;

// the boot skeleton, growing one conversion step at a time.
// step 4: printk() output goes to both the serial port and the
// screen (fbcons.cpp), as in the C version.
// step 5: typing. with no interrupts yet, main() polls the
// keyboard and the serial port by calling their interrupt
// handlers, kbdintr() and uartintr(), in a loop.
// step 6: CPU exceptions are shown on the screen (earlytrap.cpp),
// not a silent reboot; the PCI bus is read (pci.cpp).
// steps 7-10: USB keyboards, for PCs without PS/2. on such a PC
// keys come through the xHCI USB controller (xhci.cpp, usb.cpp),
// polled the same way with usbintr(). step 7 starts the controller,
// step 8 identifies devices, step 9 types, step 10 adds hubs.

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
  earlytrapinit(); // [platform: real PC] show CPU exceptions, not a reboot
  printk("\n");
  printk("xv6 kernel is booting (C++, step 9)\n");
  printk("\n");
  printbootinfo();
  kbdinit(); // PS/2 keyboard
  usbinit(); // USB keyboards, if there is an xHCI controller

  printk("\ntype on the keyboard (or the serial console): ");

  // [platform: QEMU, VirtualBox] keys come from the PS/2 keyboard
  // (the window) and from the serial port (make qemu's terminal).
  // [platform: real PC] a PS/2 keyboard, or a USB keyboard
  // (usbintr()), from whichever is there.
  // unlike hlt, this loop keeps the CPU 100% busy; interrupts
  // (later steps) will let it sleep until a key arrives.
  for (;;) {
    kbdintr();
    uartintr();
    usbintr();
    asm volatile("pause"); // a hint to the CPU that this is a spin loop
  }
}
