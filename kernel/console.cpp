//
// Console input and output, to the uart and,
// on x86-64 PCs, the screen (fbcons.c) and keyboard (kbd.c).
//
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//
// so far: consputc(), consoleinit(), and consoleintr() without
// waking up a reader. consoleread() and consolewrite() come back
// with processes; control-p (process list) with proc.cpp.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "defs.h"

// C: #define BACKSPACE 0x100 and #define C(x) ((x) - '@')
constexpr int BACKSPACE = 0x100; // erase the last output character

static constexpr int
C(char x) // Control-x
{
  return x - '@';
}

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
    // the screen first: a serial line may be slow, or blocked.
    fbconsputc('\b');
    fbconsputc(' ');
    fbconsputc('\b');
    uartputc_sync('\b');
    uartputc_sync(' ');
    uartputc_sync('\b');
  } else {
    fbconsputc(c);
    uartputc_sync(c);
  }
}

// C: #define INPUT_BUF_SIZE inside the struct. a #define has no
// scope; a constexpr declared inside an unnamed struct is not
// allowed, so it lives just outside.
constexpr uint INPUT_BUF_SIZE = 128;

static struct {
  // input circular buffer
  char buf[INPUT_BUF_SIZE];
  uint r; // Read index
  uint w; // Write index
  uint e; // Edit index
} cons;

//
// the console input interrupt handler.
// uartintr() and kbdintr() call this for each input character.
// do erase/kill processing, append to cons.buf. the C version
// then wakes up consoleread() if a whole line has arrived; there
// is no consoleread() yet, so a finished line just stays in buf.
// (the C version also takes cons.lock: one CPU, no interrupts.)
//
void
consoleintr(int c)
{
  switch (c) {
  case C('U'): // Kill line.
    while (cons.e != cons.w && cons.buf[(cons.e - 1) % INPUT_BUF_SIZE] != '\n') {
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  case C('H'): // Backspace
  case '\x7f': // Delete key
    if (cons.e != cons.w) {
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  default:
    if (c != 0 && cons.e - cons.r < INPUT_BUF_SIZE) {
      c = (c == '\r') ? '\n' : c;

      // echo back to the user.
      consputc(c);

      // store for consumption by consoleread().
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

      if (c == '\n' || c == C('D') || cons.e - cons.r == INPUT_BUF_SIZE) {
        // a whole line (or end-of-file) has arrived. the C version
        // wakes up consoleread() here. with no reader yet, consume
        // the line at once, so the buffer never fills up.
        cons.w = cons.e;
        cons.r = cons.w;
      }
    }
    break;
  }
}

void
consoleinit()
{
  uartinit();
  fbconsinit();
}
