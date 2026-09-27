//
// a console on the screen: draws text into the frame buffer that
// UEFI set up (see boot/loader.c), for machines where nobody is
// watching the serial port, such as a VirtualBox window or a real PC.
// console.c sends every character to both the UART and here.
//
// RISC-V xv6 has only the UART console.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "spinlock.h"
#include "bootinfo.h"
#include "defs.h"
#include "font.h"

// the big font on screens at least this wide.
#define BIGSCREEN 2560

#define FG 0x00D0D0D0 // light gray; the same in RGB and BGR pixel formats
#define BG 0x00000000

extern struct bootinfo bootinfo;
extern volatile int panicking; // from printk.c

static struct {
  struct spinlock lock;
  uint *fb;         // frame buffer, 32 bits per pixel
  int stride;       // pixels per scan line
  int cols, rows;   // screen size in characters
  int cx, cy;       // cursor position, in characters
  int charw, charh; // character cell size in pixels: the font size
} cons;

static void
fillrect(int x, int y, int w, int h, uint color)
{
  for (int j = 0; j < h; j++) {
    uint *p = cons.fb + (y + j) * cons.stride + x;
    for (int i = 0; i < w; i++)
      p[i] = color;
  }
}

static void
drawchar(int col, int row, int c)
{
  c &= 0x7f;
  for (int j = 0; j < cons.charh; j++) {
    uint *p = cons.fb + (row * cons.charh + j) * cons.stride + col * cons.charw;
    uint bits, top;
    if (cons.charw == 32) {
      bits = font32x64[c][j];
      top = 0x80000000;
    } else {
      bits = font16x32[c][j];
      top = 0x8000;
    }
    for (int i = 0; i < cons.charw; i++)
      p[i] = (bits & (top >> i)) ? FG : BG;
  }
}

// the cursor is an underline in the bottom rows of its cell.
static void
cursor(uint color)
{
  fillrect(cons.cx * cons.charw, cons.cy * cons.charh + cons.charh - 4,
           cons.charw, 4, color);
}

// move every line up by one, and clear the bottom line.
static void
scroll(void)
{
  uint64 *dst = (uint64 *)cons.fb;
  uint64 *src = (uint64 *)(cons.fb + cons.charh * cons.stride);
  uint64 n = (uint64)(cons.rows - 1) * cons.charh * cons.stride / 2;
  for (uint64 i = 0; i < n; i++)
    dst[i] = src[i];
  fillrect(0, (cons.rows - 1) * cons.charh, cons.cols * cons.charw, cons.charh,
           BG);
}

void
fbconsinit(void)
{
  initlock(&cons.lock, "fbcons");
  if (bootinfo.fb_base == 0)
    return; // no frame buffer: the UART is the only console
  cons.fb = (uint *)bootinfo.fb_base;
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
  fillrect(0, 0, bootinfo.fb_width, bootinfo.fb_height, BG);
  cursor(FG);
}

// draw one character at the cursor, and advance the cursor.
// \b moves the cursor back; console.c erases with "\b \b".
void
fbconsputc(int c)
{
  if (cons.fb == 0)
    return;

  // printk() may be called during a panic with locks held;
  // don't risk waiting for this lock then.
  int locked = 0;
  if (!panicking) {
    acquire(&cons.lock);
    locked = 1;
  }

  cursor(BG);
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
    drawchar(cons.cx, cons.cy, c);
    cons.cx++;
  }
  if (cons.cx >= cons.cols) {
    cons.cx = 0;
    cons.cy++;
  }
  if (cons.cy >= cons.rows) {
    scroll();
    cons.cy = cons.rows - 1;
  }
  cursor(FG);

  if (locked)
    release(&cons.lock);
}
