//
// PC keyboard (PS/2), for typing into a VirtualBox or qemu window.
// Adapted from the x86 version of xv6 (xv6-public).
// Most modern PCs have only USB keyboards, which this does not handle.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "defs.h"
#include "kbd.h"

#define KBS_IBF   0x02 // controller input buffer full
#define KBS_AUX   0x20 // the waiting byte is from the mouse, not the keyboard
#define KBC_RCONF 0x20 // read controller configuration byte
#define KBC_WCONF 0x60 // write controller configuration byte
#define KBC_AUXOFF 0xA7 // disable the mouse port
#define KBC_IRQ1  0x01 // configuration: interrupt when a key arrives
#define KBC_IRQ12 0x02 // configuration: interrupt when mouse data arrives
#define KBC_NOCLK 0x10 // configuration: keyboard clock disabled
#define KBC_NOAUXCLK 0x20 // configuration: mouse clock disabled

static void
kbcwait(void)
{
  for (int i = 0; i < 100000 && (inb(KBSTATP) & KBS_IBF); i++)
    ;
}

// UEFI firmware may have been reading the keyboard by polling,
// with the controller's keyboard interrupt turned off, and may
// have left the PS/2 mouse on. mouse bytes share the controller's
// one output buffer with keys, so unread mouse data would block
// the keyboard; turn the mouse off.
void
kbdinit(void)
{
  kbcwait();
  outb(KBSTATP, KBC_AUXOFF);

  // discard any keys typed (or mouse moves) before now.
  for (int i = 0; i < 64 && (inb(KBSTATP) & KBS_DIB); i++)
    inb(KBDATAP);

  kbcwait();
  outb(KBSTATP, KBC_RCONF);
  for (int i = 0; i < 100000 && (inb(KBSTATP) & KBS_DIB) == 0; i++)
    ;
  uchar conf = inb(KBDATAP);
  conf |= KBC_IRQ1 | KBC_NOAUXCLK;
  conf &= ~(KBC_NOCLK | KBC_IRQ12);
  kbcwait();
  outb(KBSTATP, KBC_WCONF);
  kbcwait();
  outb(KBDATAP, conf);
}

// return the next character from the keyboard, 0 if the key
// produced no character (e.g. shift), or -1 if none is waiting.
static int
kbdgetc(void)
{
  static uint shift;
  static uchar *charcode[4] = {normalmap, shiftmap, ctlmap, ctlmap};
  uint st, data, c;

  st = inb(KBSTATP);
  if ((st & KBS_DIB) == 0)
    return -1;
  data = inb(KBDATAP);
  if (st & KBS_AUX)
    return 0; // a mouse byte, in case the mouse is still on

  if (data == 0xE0) {
    shift |= E0ESC;
    return 0;
  } else if (data & 0x80) {
    // Key released
    data = (shift & E0ESC ? data : data & 0x7F);
    shift &= ~(shiftcode[data] | E0ESC);
    return 0;
  } else if (shift & E0ESC) {
    // Last character was an E0 escape; or with 0x80
    data |= 0x80;
    shift &= ~E0ESC;
  }

  shift |= shiftcode[data];
  shift ^= togglecode[data];
  c = charcode[shift & (CTL | SHIFT)][data];
  if (shift & CAPSLOCK) {
    if ('a' <= c && c <= 'z')
      c += 'A' - 'a';
    else if ('A' <= c && c <= 'Z')
      c += 'a' - 'A';
  }
  return c;
}

// handle a keyboard interrupt. called from devintr().
void
kbdintr(void)
{
  int c;
  while ((c = kbdgetc()) >= 0) {
    // skip keys without characters, and arrow keys etc.,
    // which the console does not understand.
    if (c != 0 && c < 0x80)
      consoleintr(c);
  }
}
