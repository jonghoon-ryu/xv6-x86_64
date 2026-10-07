//
// USB devices: read what a new device is (its descriptors), and
// say so. xhci.cpp does the talking to the controller. step 9
// makes keyboards type; step 10 adds hubs.
//
// not in the C version.
//

#include "types.h"
#include "param.h"
#include "x86.h"
#include "defs.h"
#include "xhci.h"

// standard requests (USB 2.0 9.4).
constexpr uchar GET_DESCRIPTOR = 6;

// descriptor types.
constexpr uchar DESC_DEVICE = 1;
constexpr uchar DESC_CONFIG = 2;
constexpr uchar DESC_INTERFACE = 4;
constexpr uchar DESC_ENDPOINT = 5;


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

    if (cls == 9) {
      printk(", a hub: ignored (hubs come in step 10)\n");
      hc.freedev(d);
      return;
    }

    KbdInfo k = findkbd(c, total);
    if (k.iface < 0 || k.ep < 0) {
      printk(", not a keyboard: ignored\n");
      hc.freedev(d);
      return;
    }
    printk(", a keyboard (typing comes in step 9)\n");
    *out = d;
    return;
  }

fail:
  printk("usb: device on port %d did not set up\n", rootport);
  hc.freedev(d);
}


