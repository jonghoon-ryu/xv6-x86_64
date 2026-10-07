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

// step 6 only: list some kinds of PCI devices, like Linux's lspci.
static int npci;

static void
pcishow(int bus, int dev, int func)
{
  uint32 id = pciread(bus, dev, func, 0x00);
  uint32 cls = pciread(bus, dev, func, 0x08) >> 8;
  printk("  %d:%d.%d  vendor %x device %x  class %x\n", bus, dev, func,
         id & 0xFFFF, id >> 16, cls);
  npci++;
}

static void
pcilist()
{
  static const struct {
    uint32 cls; // class, subclass, programming interface
    const char *name;
  } kinds[] = {
    { 0x030000, "display (VGA)" },
    { 0x010601, "SATA disk controller (AHCI)" },
    { 0x010802, "NVMe disk" },
    { 0x020000, "Ethernet" },
    { 0x0C0320, "USB 2 controller (EHCI)" },
    { 0x0C0330, "USB 3 controller (xHCI)" },
  };
  for (auto &k : kinds) {
    printk("PCI %s:\n", k.name);
    npci = 0;
    pciscan(k.cls, pcishow);
    if (npci == 0)
      printk("  none\n");
  }
}

// start() jumps here on the first CPU.
void
main()
{
  consoleinit();
  earlytrapinit(); // [platform: real PC] show CPU exceptions, not a reboot
  printk("\n");
  printk("xv6 kernel is booting (C++, step 6)\n");
  printk("\n");
  printbootinfo();
  kbdinit(); // PS/2 keyboard
  pcilist(); // step 6 only

  printk("\ntype on the keyboard (or the serial console): ");

  // [platform: QEMU, VirtualBox] keys come from the PS/2 keyboard
  // (the window) and from the serial port (make qemu's terminal).
  // [platform: real PC] only a PS/2 keyboard, or a USB keyboard
  // that the firmware still emulates as PS/2; see kbd.cpp.
  // unlike hlt, this loop keeps the CPU 100% busy; interrupts
  // (later steps) will let it sleep until a key arrives.
  for (;;) {
    kbdintr();
    uartintr();
    asm volatile("pause"); // a hint to the CPU that this is a spin loop
  }
}
