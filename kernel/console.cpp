//
// Console input and output, to the uart and,
// on x86-64 PCs, the screen (fbcons.c) and keyboard (kbd.c).
//
// so far only the output half of console.c: consputc() and
// consoleinit(). the input side (consoleintr, consoleread) and
// consolewrite come back with interrupts and processes.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "defs.h"

// C: #define BACKSPACE 0x100
constexpr int BACKSPACE = 0x100; // erase the last output character

//
// send one character to the uart, but don't use
// interrupts or sleep(). safe to be called from
// interrupts, e.g. by printk and to echo input
// characters.
//
void
consputc(int c)
{
  if (c == BACKSPACE) {
    // if the user typed backspace, overwrite with a space.
    uartputc_sync('\b');
    uartputc_sync(' ');
    uartputc_sync('\b');
  } else {
    uartputc_sync(c);
  }
}

void
consoleinit()
{
  uartinit();
}
