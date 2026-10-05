//
// low-level driver for 16550a UART.
//
// so far: uartinit(), uartputc_sync(), uartgetc(), and uartintr()
// without its wakeup of a sending thread. uartwrite(), which
// needs sleep(), comes back with processes.
// step 5: nothing handles interrupts yet, so main() calls
// uartintr() over and over (polling).
//
// [platform: QEMU, VirtualBox] both emulate a 16550 at COM1. make
// qemu connects it to the terminal; make vbox to vbox/serial.log.
// [platform: real PC] most PCs have no COM1. reading a missing
// port gives 0xFF, so LSR_TX_IDLE always looks set: output is
// thrown away instead of hanging, and uartgetc() must not trust
// LSR_RX_READY alone (see there).
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "defs.h"

// the UART control registers, at I/O ports COM1 + reg.
// some have different meanings for read vs write.
// see http://byterunner.com/16550.html
// C: #define constants; C++: typed constants the compiler checks.
constexpr ushort RHR = 0;                  // receive holding register (for input bytes)
constexpr ushort THR = 0;                  // transmit holding register (for output bytes)
constexpr ushort IER = 1;                  // interrupt enable register
constexpr ushort FCR = 2;                  // FIFO control register
constexpr ushort ISR = 2;                  // interrupt status register
constexpr uchar FCR_FIFO_ENABLE = 1 << 0;
constexpr uchar FCR_FIFO_CLEAR = 3 << 1;   // clear the content of the two FIFOs
constexpr ushort LCR = 3;                  // line control register
constexpr uchar LCR_EIGHT_BITS = 3 << 0;
constexpr uchar LCR_BAUD_LATCH = 1 << 7;   // special mode to set baud rate
constexpr ushort LSR = 5;                  // line status register
constexpr uchar LSR_RX_READY = 1 << 0;     // input is waiting to be read from RHR
constexpr uchar LSR_TX_IDLE = 1 << 5;      // THR can accept another character to send

// C: #define ReadReg(reg) (inb(COM1 + (reg))); C++: inline functions.
static inline uchar
ReadReg(ushort reg)
{
  return inb(COM1 + reg);
}

static inline void
WriteReg(ushort reg, uchar v)
{
  outb(COM1 + reg, v);
}

void
uartinit()
{
  // disable interrupts.
  WriteReg(IER, 0x00);

  // special mode to set baud rate.
  WriteReg(LCR, LCR_BAUD_LATCH);

  // LSB for baud rate of 38.4K.
  WriteReg(0, 0x03);

  // MSB for baud rate of 38.4K.
  WriteReg(1, 0x00);

  // leave set-baud mode,
  // and set word length to 8 bits, no parity.
  WriteReg(LCR, LCR_EIGHT_BITS);

  // reset and enable FIFOs.
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  // the C version enables transmit and receive interrupts here.
  // nothing handles interrupts yet, so they stay off.
}

// write a byte to the uart without using interrupts, for use by
// printk() and to echo characters. it spins waiting for the uart's
// output register to be empty.
void
uartputc_sync(int c)
{
  // wait for UART to set Transmit Holding Empty in LSR.
  while ((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);
}

// try to read one input character from the UART.
// return -1 if none is waiting.
// (public in steps 2-4, before uartintr() existed.)
static int
uartgetc()
{
  uchar lsr = ReadReg(LSR);
  // [platform: real PC] no UART: every register reads 0xFF.
  if (lsr == 0xFF)
    return -1;
  if (lsr & LSR_RX_READY)
    return ReadReg(RHR);
  return -1;
}

// handle a uart interrupt, raised because input has
// arrived, or the uart is ready for more output, or
// both. called from devintr() in the C version; for now
// main() calls it in a loop.
void
uartintr()
{
  ReadReg(ISR); // acknowledge the interrupt

  // the C version wakes up a thread in uartwrite() here when the
  // UART is ready for more output. no uartwrite() yet.

  // read and process incoming characters, if any.
  while (1) {
    int c = uartgetc();
    if (c == -1)
      break;
    consoleintr(c);
  }
}
