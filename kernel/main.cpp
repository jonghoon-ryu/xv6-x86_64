#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "bootinfo.h"
#include "defs.h"

extern struct bootinfo bootinfo;

// the boot skeleton, growing one conversion step at a time.
// step 3: output goes through printk() and the console. until
// fbcons.cpp (step 4) comes back, it reaches only the serial port,
// and main() still paints the screen directly.

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

// fill the UEFI frame buffer with one color, so that a VirtualBox
// window or a real PC's screen shows the kernel got control.
static void
paintscreen(uint color)
{
  if (bootinfo.fb_base == 0)
    return;
  auto fb = reinterpret_cast<uint *>(bootinfo.fb_base);
  for (uint64 y = 0; y < bootinfo.fb_height; y++)
    for (uint64 x = 0; x < bootinfo.fb_width; x++)
      fb[y * bootinfo.fb_stride + x] = color;
}

// three hand-drawn 8x8 letters, one byte per row, leftmost pixel
// in the top bit. fbcons.c's real font replaces them later.
static const uchar glyph_x[8] = { 0x00, 0x00, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0x00 };
static const uchar glyph_v[8] = { 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00 };
static const uchar glyph_6[8] = { 0x3C, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0x00 };

// draw an 8x8 glyph with its top-left corner at (x0, y0), each
// font pixel as a scale x scale square.
static void
drawglyph(const uchar glyph[8], uint64 x0, uint64 y0, uint64 scale, uint color)
{
  if (bootinfo.fb_base == 0)
    return;
  auto fb = reinterpret_cast<uint *>(bootinfo.fb_base);
  for (uint64 y = 0; y < 8 * scale && y0 + y < bootinfo.fb_height; y++)
    for (uint64 x = 0; x < 8 * scale && x0 + x < bootinfo.fb_width; x++)
      if (glyph[y / scale] & (0x80 >> (x / scale)))
        fb[(y0 + y) * bootinfo.fb_stride + x0 + x] = color;
}

// start() jumps here on the first CPU.
void
main()
{
  consoleinit();
  printk("\n");
  printk("xv6 kernel is booting (C++, step 3)\n");
  printk("\n");
  printbootinfo();
  paintscreen(0x00203060); // dark blue

  // "xv6" in white (the same in RGB and BGR pixel formats),
  // each letter 8 * scale pixels wide.
  uint64 scale = bootinfo.fb_width / 80;
  const uchar *word[] = { glyph_x, glyph_v, glyph_6 };
  for (int i = 0; i < 3; i++)
    drawglyph(word[i], scale * 8 * (1 + i), scale * 8, scale, 0x00FFFFFF);

  printk("nothing else yet; halting\n");

  for (;;)
    asm volatile("hlt");
}
