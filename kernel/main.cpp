#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "bootinfo.h"
#include "defs.h"

extern struct bootinfo bootinfo;

// the boot skeleton, growing one conversion step at a time.
// step 2: serial output goes through uart.cpp. until printk.cpp
// (step 3) and fbcons.cpp (step 4) come back, main() still writes
// strings itself and paints the screen directly.

static void
uartputs(const char *s)
{
  for (; *s; s++)
    uartputc_sync(*s);
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
  uartinit();
  uartputs("\nxv6 kernel is booting (C++, step 2)\n");
  paintscreen(0x00203060); // dark blue

  // "xv6" in white (the same in RGB and BGR pixel formats),
  // each letter 8 * scale pixels wide.
  uint64 scale = bootinfo.fb_width / 80;
  const uchar *word[] = { glyph_x, glyph_v, glyph_6 };
  for (int i = 0; i < 3; i++)
    drawglyph(word[i], scale * 8 * (1 + i), scale * 8, scale, 0x00FFFFFF);

  uartputs("nothing else yet; halting\n");

  for (;;)
    asm volatile("hlt");
}
