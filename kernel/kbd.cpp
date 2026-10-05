//
// PC keyboard (PS/2), for typing into a VirtualBox or qemu window.
// Adapted from the x86 version of xv6 (xv6-public).
// Most modern PCs have only USB keyboards, which this does not handle.
//
// [platform: QEMU, VirtualBox] both emulate a PS/2 keyboard
// controller; keys typed into the window arrive here.
// [platform: real PC] works with a PS/2 keyboard, or with a USB
// keyboard if the firmware's "legacy USB support" keeps emulating
// PS/2 after the kernel starts (many do not, especially with CSM
// off). with no controller at all, every port reads 0xFF; see
// kbdgetc().
//
// step 5: nothing handles interrupts yet, so main() calls
// kbdintr() over and over (polling) instead of the interrupt
// handler calling it.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "defs.h"
#include "kbd.h"

constexpr uchar KBS_IBF = 0x02;      // controller input buffer full
constexpr uchar KBS_AUX = 0x20;      // the waiting byte is from the mouse, not the keyboard
constexpr uchar KBC_RCONF = 0x20;    // read controller configuration byte
constexpr uchar KBC_WCONF = 0x60;    // write controller configuration byte
constexpr uchar KBC_AUXOFF = 0xA7;   // disable the mouse port
constexpr uchar KBC_IRQ1 = 0x01;     // configuration: interrupt when a key arrives
constexpr uchar KBC_IRQ12 = 0x02;    // configuration: interrupt when mouse data arrives
constexpr uchar KBC_NOCLK = 0x10;    // configuration: keyboard clock disabled
constexpr uchar KBC_NOAUXCLK = 0x20; // configuration: mouse clock disabled

static void
kbcwait()
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
kbdinit()
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
  // the keyboard interrupt is turned on as in the C version; the
  // CPU ignores it for now (interrupts are off since entry.S).
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
kbdgetc()
{
  static uint shift;
  // C: static uchar *charcode[4] = {normalmap, shiftmap, ctlmap, ctlmap};
  static const Keymap *charcode[4] = {&normalmap, &shiftmap, &ctlmap, &ctlmap};
  uint st, data, c;

  st = inb(KBSTATP);
  // [platform: real PC] no keyboard controller: the status port
  // reads 0xFF, which would look like an endless stream of keys.
  // (not in the C version, where only the interrupt calls this.)
  if (st == 0xFF)
    return -1;
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
  c = (*charcode[shift & (CTL | SHIFT)])[data];
  if (shift & CAPSLOCK) {
    if ('a' <= c && c <= 'z')
      c += 'A' - 'a';
    else if ('A' <= c && c <= 'Z')
      c += 'a' - 'A';
  }
  return c;
}

// handle a keyboard interrupt. called from devintr() in the C
// version; for now main() calls it in a loop.
void
kbdintr()
{
  int c;
  while ((c = kbdgetc()) >= 0) {
    // skip keys without characters, and arrow keys etc.,
    // which the console does not understand.
    if (c != 0 && c < 0x80)
      consoleintr(c);
  }
}
