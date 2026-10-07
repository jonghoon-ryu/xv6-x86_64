//
// USB devices: read what a new device is (its descriptors), set up
// hubs and keyboards, and turn keyboard reports into characters
// for consoleintr(). xhci.cpp does the talking to the controller.
//
// keyboards are used in the "boot protocol" (USB HID 1.11,
// appendix B): a fixed 8-byte report that every keyboard supports
// so that PC firmware can use it without a full HID parser.
//
// not in the C version.
//

#include "types.h"
#include "param.h"
#include "x86.h"
#include "defs.h"
#include "xhci.h"

// standard requests (USB 2.0 9.4).
constexpr uchar GET_STATUS = 0;
constexpr uchar CLEAR_FEATURE = 1;
constexpr uchar SET_FEATURE = 3;
constexpr uchar GET_DESCRIPTOR = 6;
constexpr uchar SET_CONFIGURATION = 9;
// HID class requests (USB HID 1.11 7.2).
constexpr uchar SET_IDLE = 0x0A;
constexpr uchar SET_PROTOCOL = 0x0B;

// descriptor types.
constexpr uchar DESC_DEVICE = 1;
constexpr uchar DESC_CONFIG = 2;
constexpr uchar DESC_INTERFACE = 4;
constexpr uchar DESC_ENDPOINT = 5;
constexpr uchar DESC_HUB = 0x29;

// hub port features and status bits (USB 2.0 11.24.2).
constexpr ushort PORT_RESET = 4;
constexpr ushort PORT_POWER = 8;
constexpr ushort C_PORT_CONNECTION = 16;
constexpr ushort C_PORT_RESET = 20;

constexpr int MAXDEPTH = 5; // hubs on the way, at most (USB 2.0 4.1.1)

static const char *
speedname(int speed)
{
  switch (speed) {
  case SPEED_LOW: return "low";
  case SPEED_FULL: return "full";
  case SPEED_HIGH: return "high";
  case SPEED_SUPER: return "super";
  }
  return "unknown";
}

static void hub(UsbDev &d, int nports, int ttt);

// a keyboard's interrupt IN endpoint, found in the configuration.
struct KbdInfo {
  int iface = -1;
  int ep = -1;
  int mps;
  int interval;
};

// look through the configuration descriptor (interfaces, each
// followed by its endpoints) for a boot keyboard.
static KbdInfo
findkbd(uchar *c, int len)
{
  KbdInfo k;
  bool inkbd = false;
  for (int i = 0; i + 2 <= len && c[i] >= 2; i += c[i]) {
    if (c[i + 1] == DESC_INTERFACE && i + 9 <= len) {
      // class 3 (HID), subclass 1 (boot), protocol 1 (keyboard).
      inkbd = k.iface < 0 && c[i + 5] == 3 && c[i + 6] == 1 && c[i + 7] == 1;
      if (inkbd)
        k.iface = c[i + 2];
    } else if (c[i + 1] == DESC_ENDPOINT && i + 7 <= len && inkbd && k.ep < 0) {
      // an interrupt (attributes 3) IN (address bit 7) endpoint.
      if ((c[i + 2] & 0x80) && (c[i + 3] & 3) == 3) {
        k.ep = c[i + 2] & 0xF;
        k.mps = (c[i + 4] | (c[i + 5] << 8)) & 0x7FF;
        k.interval = c[i + 6];
      }
    }
  }
  return k;
}

// the controller's interval (2^n x 125 us, xHCI 6.2.3.6) from the
// endpoint descriptor's bInterval, which counts milliseconds at
// low and full speed and is already an exponent at high speed.
static int
xinterval(int speed, int binterval)
{
  if (speed == SPEED_HIGH || speed == SPEED_SUPER) {
    int n = binterval - 1;
    return n < 0 ? 0 : n > 15 ? 15 : n;
  }
  int n = 3; // 1 ms
  while (n < 10 && (1 << (n + 1)) <= binterval * 8)
    n++;
  return n;
}

static bool
kbdsetup(UsbDev &d, const KbdInfo &k, int config)
{
  Xhci &hc = *d.hc;
  int dci = 2 * k.ep + 1; // device context index of an IN endpoint
  int mps = k.mps > 64 ? 64 : k.mps;
  d.kbdiface = k.iface;
  d.kbddci = dci;
  d.kbdmps = mps;
  if (!hc.configep(d, dci, 7, mps, xinterval(d.speed, k.interval))) // 7: interrupt IN
    return false;
  if (!hc.control(d, 0x00, SET_CONFIGURATION, config, 0, 0, nullptr))
    return false;
  // boot protocol (0). keyboards start in it after a reset, but
  // the firmware may have switched this one to report protocol.
  if (!hc.control(d, 0x21, SET_PROTOCOL, 0, k.iface, 0, nullptr))
    printk("usb: keyboard refused SET_PROTOCOL\n");
  // report only when keys change. many keyboards refuse; harmless.
  hc.control(d, 0x21, SET_IDLE, 0, k.iface, 0, nullptr);
  d.report = d.buf + 2048; // control transfers use the first half
  memset(d.prev, 0, sizeof(d.prev));
  d.kbd = true;
  hc.queuein(d);
  return true;
}

// a device appeared on a port: on the controller's own port
// rootport if route is 0, else behind hubs (xHCI 4.3).
// *out is set to the device, to forget it when it goes away.
void
usbattach(Xhci &hc, int speed, int rootport, uint32 route, int depth,
          int ttslot, int ttport, UsbDev **out)
{
  UsbDev *d = hc.newdev(speed, rootport, route, depth, ttslot, ttport);
  if (d == nullptr)
    return;
  if (!hc.addressdevice(*d)) {
    hc.freedev(d);
    return;
  }
  microdelay(10 * 1000); // at least 2 ms after SET_ADDRESS (USB 2.0 9.2.6.3)

  // the device descriptor's first 8 bytes hold endpoint 0's packet
  // size, which full speed devices choose (8, 16, 32 or 64).
  uchar dd[18];
  if (!hc.control(*d, 0x80, GET_DESCRIPTOR, DESC_DEVICE << 8, 0, 8, dd))
    goto fail;
  if (speed == SPEED_FULL && dd[7] != d->mps0) {
    d->mps0 = dd[7];
    if (!hc.setmps0(*d))
      goto fail;
  }
  if (!hc.control(*d, 0x80, GET_DESCRIPTOR, DESC_DEVICE << 8, 0, 18, dd))
    goto fail;

  {
    int vendor = dd[8] | (dd[9] << 8), product = dd[10] | (dd[11] << 8);
    int cls = dd[4];
    printk("usb: port %d", rootport);
    for (int i = 0; i < depth; i++)
      printk(".%d", (route >> (4 * i)) & 0xF);
    printk(": %s speed, id %x:%x, class %d", speedname(speed), vendor, product, cls);

    // the first configuration: 9 bytes, then all wTotalLength.
    uchar *c = d->buf + 1024; // stays put: control() copies into it
    uchar head[9];
    if (!hc.control(*d, 0x80, GET_DESCRIPTOR, DESC_CONFIG << 8, 0, 9, head)) {
      printk("\n");
      goto fail;
    }
    int total = head[2] | (head[3] << 8);
    if (total > 1024)
      total = 1024;
    if (!hc.control(*d, 0x80, GET_DESCRIPTOR, DESC_CONFIG << 8, 0, total, c)) {
      printk("\n");
      goto fail;
    }
    int config = head[5];

    if (cls == 9) {
      printk(", a hub\n");
      if (depth >= MAXDEPTH ||
          !hc.control(*d, 0x00, SET_CONFIGURATION, config, 0, 0, nullptr))
        goto fail;
      uchar hd[9];
      if (!hc.control(*d, 0xA0, GET_DESCRIPTOR, DESC_HUB << 8, 0, 9, hd))
        goto fail;
      int nports = hd[2];
      int ttt = (hd[3] >> 5) & 3; // TT think time
      if (!hc.sethub(*d, nports, ttt))
        goto fail;
      *out = d;
      hub(*d, nports, hd[5]);
      return;
    }

    KbdInfo k = findkbd(c, total);
    if (k.iface < 0 || k.ep < 0) {
      printk(", not a keyboard: ignored\n");
      hc.freedev(d);
      return;
    }
    printk(", a keyboard\n");
    if (!kbdsetup(*d, k, config))
      goto fail;
    *out = d;
    printk("usb: keyboard ready: type on it\n");
    return;
  }

fail:
  printk("usb: device on port %d did not set up\n", rootport);
  hc.freedev(d);
}

// a hub's own port status (USB 2.0 11.24.2.7): wPortStatus in the
// low 16 bits, wPortChange in the high 16.
static bool
hubportstatus(UsbDev &d, int port, uint32 *st)
{
  uchar b[4];
  if (!d.hc->control(d, 0xA3, GET_STATUS, 0, port, 4, b))
    return false;
  *st = b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
  return true;
}

// set up the devices on a hub's ports. only those connected now:
// xv6 does not watch hubs for devices plugged in later.
static void
hub(UsbDev &d, int nports, int pwrgood)
{
  Xhci &hc = *d.hc;
  for (int p = 1; p <= nports; p++)
    hc.control(d, 0x23, SET_FEATURE, PORT_POWER, p, 0, nullptr);
  microdelay((pwrgood * 2 + 100) * 1000); // power on, then let devices connect

  for (int p = 1; p <= nports && p <= 15; p++) {
    uint32 st;
    if (!hubportstatus(d, p, &st) || (st & 1) == 0) // bit 0: connected
      continue;
    hc.control(d, 0x23, CLEAR_FEATURE, C_PORT_CONNECTION, p, 0, nullptr);
    hc.control(d, 0x23, SET_FEATURE, PORT_RESET, p, 0, nullptr);
    bool done = false;
    for (int i = 0; i < 50 && !done; i++) {
      microdelay(10 * 1000);
      done = hubportstatus(d, p, &st) && (st & (1 << (16 + 4))); // reset changed
    }
    if (!done || (st & 2) == 0) { // bit 1: enabled
      printk("usb: hub port %d does not enable\n", p);
      continue;
    }
    hc.control(d, 0x23, CLEAR_FEATURE, C_PORT_RESET, p, 0, nullptr);
    microdelay(10 * 1000); // reset recovery

    int speed = (st & (1 << 9)) ? SPEED_LOW : (st & (1 << 10)) ? SPEED_HIGH : SPEED_FULL;
    // a low or full speed device behind a high speed hub talks
    // through the hub's Transaction Translator; behind a full
    // speed hub, through whatever translator that hub uses.
    int ttslot = d.ttslot, ttport = d.ttport;
    if (d.speed == SPEED_HIGH && speed != SPEED_HIGH) {
      ttslot = d.slot;
      ttport = p;
    }
    UsbDev *child = nullptr;
    usbattach(hc, speed, d.rootport, d.route | (p << (4 * d.depth)), d.depth + 1,
              ttslot, ttport, &child);
  }
}

// ---------------------------------------------------------------
// keyboard reports

// HID usage IDs 0x04-0x38 (USB HID Usage Tables, section 10), as
// characters, without and with shift.
static const char usbmap[2][0x39] = {
  { 0,    0,    0,    0,    'a',  'b',  'c',  'd',  'e',  'f',  'g',  'h',
    'i',  'j',  'k',  'l',  'm',  'n',  'o',  'p',  'q',  'r',  's',  't',
    'u',  'v',  'w',  'x',  'y',  'z',  '1',  '2',  '3',  '4',  '5',  '6',
    '7',  '8',  '9',  '0',  '\n', 0x1B, '\b', '\t', ' ',  '-',  '=',  '[',
    ']',  '\\', '\\', ';',  '\'', '`',  ',',  '.',  '/' },
  { 0,    0,    0,    0,    'A',  'B',  'C',  'D',  'E',  'F',  'G',  'H',
    'I',  'J',  'K',  'L',  'M',  'N',  'O',  'P',  'Q',  'R',  'S',  'T',
    'U',  'V',  'W',  'X',  'Y',  'Z',  '!',  '@',  '#',  '$',  '%',  '^',
    '&',  '*',  '(',  ')',  '\n', 0x1B, '\b', '\t', ' ',  '_',  '+',  '{',
    '}',  '|',  '|',  ':',  '"',  '~',  '<',  '>',  '?' },
};

// the keypad (usage IDs 0x54-0x63), as if Num Lock were on.
static const char keypad[] = "/*-+\n1234567890.";

constexpr int KEY_CAPSLOCK = 0x39;
constexpr uchar MOD_CTRL = 0x11;  // left or right Ctrl
constexpr uchar MOD_SHIFT = 0x22; // left or right Shift

static int
usbkey(int usage, uchar mod)
{
  static bool capslock;
  int c = 0;
  if (usage == KEY_CAPSLOCK) {
    capslock = !capslock;
    return 0;
  }
  if (usage < 0x39)
    c = usbmap[(mod & MOD_SHIFT) ? 1 : 0][usage];
  else if (usage >= 0x54 && usage <= 0x63)
    c = keypad[usage - 0x54];
  if (capslock && 'a' <= c && c <= 'z')
    c += 'A' - 'a';
  else if (capslock && 'A' <= c && c <= 'Z')
    c += 'a' - 'A';
  if ((mod & MOD_CTRL) && c >= '@' && c < 0x7F)
    c &= 0x1F; // Ctrl-A is 1, as C('A') in console.cpp
  return c;
}

// a report arrived: modifier bits, a reserved byte, then up to 6
// keys held down. a key is new if it was not in the last report.
// (the keyboard sends nothing while a key stays down, so holding
// a key does not repeat it: that needs a timer, which comes later.)
void
usbkbdreport(UsbDev &d, int len)
{
  uchar *r = d.report;
  if (len < 3 || r[2] == 1) // 1: too many keys down at once
    return;
  if (len > 8)
    len = 8;
  for (int i = 2; i < len; i++) {
    if (r[i] == 0)
      continue;
    bool old = false;
    for (int j = 2; j < 8; j++)
      if (d.prev[j] == r[i])
        old = true;
    if (!old) {
      int c = usbkey(r[i], r[0]);
      if (c != 0)
        consoleintr(c);
    }
  }
  memset(d.prev, 0, sizeof(d.prev));
  memmove(d.prev, r, len);
}
