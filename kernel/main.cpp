#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "bootinfo.h"

extern struct bootinfo bootinfo;

// the boot skeleton: just enough to show the kernel is running.
// the real console (uart.c, console.c, printk.c, fbcons.c) comes
// back, in C++, as the first conversion step; until then main()
// talks to the serial port directly and paints the screen.

// the 16550 UART registers used here; see uart.c in the C port.
constexpr int THR = 0;              // transmit holding register
constexpr int IER = 1;              // interrupt enable register
constexpr int FCR = 2;              // FIFO control register
constexpr int LCR = 3;              // line control register
constexpr int LSR = 5;              // line status register
constexpr int LCR_BAUD_LATCH = 0x80;
constexpr int LCR_EIGHT_BITS = 0x03;
constexpr int LSR_TX_IDLE = 0x20;

static void
earlyuartinit()
{
  outb(COM1 + IER, 0x00);           // no interrupts
  outb(COM1 + LCR, LCR_BAUD_LATCH); // set the baud rate:
  outb(COM1 + 0, 0x03);             // 38.4K
  outb(COM1 + 1, 0x00);
  outb(COM1 + LCR, LCR_EIGHT_BITS); // 8 bits, no parity
  outb(COM1 + FCR, 0x07);           // reset and enable FIFOs
}

static void
earlyputs(const char *s)
{
  for (; *s; s++) {
    if (*s == '\n')
      earlyputs("\r");
    while ((inb(COM1 + LSR) & LSR_TX_IDLE) == 0)
      ;
    outb(COM1 + THR, *s);
  }
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

// start() jumps here on the first CPU.
void
main()
{
  earlyuartinit();
  earlyputs("\nxv6 kernel is booting (C++ skeleton)\n");
  paintscreen(0x00203060); // dark blue
  earlyputs("nothing else yet; halting\n");

  for (;;)
    asm volatile("hlt");
}
