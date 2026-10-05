//
// a console on the screen: draws text into the frame buffer that
// UEFI set up (see boot/loader.c), for machines where nobody is
// watching the serial port, such as a VirtualBox window or a real PC.
// console.cpp sends every character to both the UART and here.
//
// RISC-V xv6 has only the UART console.
//
// [platform: real PC] usually the only console: few PCs have a
// serial port. [platform: QEMU, VirtualBox] the same text also
// goes to the serial port.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "bootinfo.h"
#include "defs.h"
#include "font.h"

extern struct bootinfo bootinfo;

// the big font on screens at least this wide.
// [platform: VirtualBox] vbox.sh sets 2560x1440, so VirtualBox gets
// the 32x64 font; QEMU's 1280x800 gets 16x32. [platform: real PC]
// depends on the monitor: 4K screens get the big font.
constexpr uint64 BIGSCREEN = 2560;

constexpr uint FG = 0x00D0D0D0; // light gray; the same in RGB and BGR pixel formats
constexpr uint BG = 0x00000000;

namespace {

// the C version keeps the same fields in a static struct and has
// free functions fillrect(), drawchar(), cursor(), scroll() that
// use it; here they are its member functions.
// (the C version's cons.lock comes back with spinlock.cpp.)
struct FbCons {
  uint *fb;         // frame buffer, 32 bits per pixel
  int stride;       // pixels per scan line
  int cols, rows;   // screen size in characters
  int cx, cy;       // cursor position, in characters
  int charw, charh; // character cell size in pixels: the font size

  void
  fillrect(int x, int y, int w, int h, uint color)
  {
    for (int j = 0; j < h; j++) {
      uint *p = fb + (y + j) * stride + x;
      for (int i = 0; i < w; i++)
        p[i] = color;
    }
  }

  void
  drawchar(int col, int row, int c)
  {
    c &= 0x7f;
    for (int j = 0; j < charh; j++) {
      uint *p = fb + (row * charh + j) * stride + col * charw;
      uint bits, top;
      if (charw == 32) {
        bits = font32x64[c][j];
        top = 0x80000000;
      } else {
        bits = font16x32[c][j];
        top = 0x8000;
      }
      for (int i = 0; i < charw; i++)
        p[i] = (bits & (top >> i)) ? FG : BG;
    }
  }

  // the cursor is an underline in the bottom rows of its cell.
  void
  cursor(uint color)
  {
    fillrect(cx * charw, cy * charh + charh - 4, charw, 4, color);
  }

  // move every line up by one, and clear the bottom line.
  void
  scroll()
  {
    auto dst = reinterpret_cast<uint64 *>(fb);
    auto src = reinterpret_cast<uint64 *>(fb + charh * stride);
    uint64 n = (uint64)(rows - 1) * charh * stride / 2;
    for (uint64 i = 0; i < n; i++)
      dst[i] = src[i];
    fillrect(0, (rows - 1) * charh, cols * charw, charh, BG);
  }
};

// zero-initialized, like the C version's static struct: no
// constructor, so start() has nothing to run for it.
FbCons cons;

} // namespace

void
fbconsinit()
{
  if (bootinfo.fb_base == 0)
    return; // no frame buffer: the UART is the only console
  cons.fb = reinterpret_cast<uint *>(bootinfo.fb_base);
  cons.stride = bootinfo.fb_stride;
  if (bootinfo.fb_width >= BIGSCREEN) {
    cons.charw = 32;
    cons.charh = 64;
  } else {
    cons.charw = 16;
    cons.charh = 32;
  }
  cons.cols = bootinfo.fb_width / cons.charw;
  cons.rows = bootinfo.fb_height / cons.charh;
  cons.fillrect(0, 0, bootinfo.fb_width, bootinfo.fb_height, BG);
  cons.cursor(FG);
}

// draw one character at the cursor, and advance the cursor.
// \b moves the cursor back; console.cpp erases with "\b \b".
void
fbconsputc(int c)
{
  if (cons.fb == nullptr)
    return;

  cons.cursor(BG);
  if (c == '\n') {
    cons.cx = 0;
    cons.cy++;
  } else if (c == '\r') {
    cons.cx = 0;
  } else if (c == '\b') {
    if (cons.cx > 0)
      cons.cx--;
  } else if (c == '\t') {
    cons.cx = (cons.cx + 8) & ~7;
  } else {
    cons.drawchar(cons.cx, cons.cy, c);
    cons.cx++;
  }
  if (cons.cx >= cons.cols) {
    cons.cx = 0;
    cons.cy++;
  }
  if (cons.cy >= cons.rows) {
    cons.scroll();
    cons.cy = cons.rows - 1;
  }
  cons.cursor(FG);
}
