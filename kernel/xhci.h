// the USB 3 host controller (xHCI) and the USB devices behind it.
// xhci.cpp drives the controller; usb.cpp sets up the devices
// (hubs and keyboards) and turns key reports into characters.
//
// not in the C version, which has no USB. the names follow the
// xHCI specification (revision 1.2), so that the code can be
// checked against it: section numbers are given as "xHCI 4.6.5".
#pragma once

// a Transfer Request Block (xHCI 4.11): the 16-byte unit of every
// ring. the controller reads and writes these in memory (DMA), so
// they are volatile.
struct Trb {
  volatile uint64 param;
  volatile uint32 status;
  volatile uint32 control; // bit 0: cycle; bits 15:10: TRB type
};

// TRB types (xHCI 6.4.6).
constexpr uint32 TRB_SETUP = 2;
constexpr uint32 TRB_DATA = 3;
constexpr uint32 TRB_STATUS = 4;
constexpr uint32 TRB_LINK = 6;
constexpr uint32 TRB_ENABLE_SLOT = 9;
constexpr uint32 TRB_DISABLE_SLOT = 10;
constexpr uint32 TRB_ADDRESS_DEVICE = 11;
constexpr uint32 TRB_EVALUATE_CONTEXT = 13;
constexpr uint32 TRB_RESET_EP = 14;
constexpr uint32 TRB_SET_TR_DEQUEUE = 16;
constexpr uint32 TRB_TRANSFER_EVENT = 32;
constexpr uint32 TRB_COMMAND_COMPLETION = 33;
constexpr uint32 TRB_PORT_STATUS_CHANGE = 34;

// completion codes (xHCI 6.4.5).
constexpr int CC_SUCCESS = 1;
constexpr int CC_STALL = 6;
constexpr int CC_SHORT_PACKET = 13;

// port speeds, as in PORTSC and the slot context (xHCI 7.2.2.1.1).
constexpr int SPEED_FULL = 1;  // USB 1.1, 12 Mb/s
constexpr int SPEED_LOW = 2;   // USB 1.1, 1.5 Mb/s: most keyboards
constexpr int SPEED_HIGH = 3;  // USB 2.0, 480 Mb/s
constexpr int SPEED_SUPER = 4; // USB 3.x

// a ring of TRBs in one page (xHCI 4.9): a command ring, or a
// transfer ring for one endpoint. software adds TRBs at idx; the
// controller consumes them. the last TRB links back to the first.
// cycle is the bit that marks a TRB as new on this trip around
// the ring: it flips each time the ring wraps (xHCI 4.9.2).
struct Ring {
  static constexpr int NTRB = 4096 / sizeof(Trb); // 256, the last a link
  Trb *trb;
  int idx;
  uint32 cycle;

  void init();
  Trb *push(uint64 param, uint32 status, uint32 control);
  // where the controller should continue, for Set TR Dequeue Pointer.
  uint64 dequeue() const { return (uint64)&trb[idx] | cycle; }
};

class Xhci;

// one USB device: a keyboard, a hub, or something xv6 ignores.
struct UsbDev {
  Xhci *hc;
  int slot;        // the controller's number for this device
  int speed;       // SPEED_*
  int rootport;    // the controller's port the device is behind (from 1)
  uint32 route;    // the hub ports on the way there (xHCI 8.9)
  int depth;       // how many hubs are on the way
  int ttslot;      // a low/full speed device behind a high speed hub:
  int ttport;      //   that hub's slot and port (its Transaction Translator)
  int mps0;        // endpoint 0's maximum packet size
  Ring ep0;        // endpoint 0: control transfers
  uchar *inctx;    // input context: what software asks for (xHCI 6.2.5)
  uchar *outctx;   // device context: the controller's view (xHCI 6.2.1)
  uchar *buf;      // DMA buffer for control transfers
  // the result of the last control transfer, set by Xhci::poll().
  volatile bool ctldone;
  volatile int ctlcc;
};

// one xHCI controller. a PC may have several (AMD chipsets often
// have two or three); each gets one of these.
class Xhci {
public:
  bool init(int bus, int dev, int func);
  void poll();    // handle events from the controller
  void service(); // handle connected and disconnected ports

  // used by usb.cpp to set up devices.
  UsbDev *newdev(int speed, int rootport, uint32 route, int depth,
                 int ttslot, int ttport);
  void freedev(UsbDev *d);
  bool addressdevice(UsbDev &d);
  bool setmps0(UsbDev &d);
  bool control(UsbDev &d, uchar reqtype, uchar req, ushort value,
               ushort index, ushort len, void *data);
  int id;

private:
  static constexpr int MAXSLOTS = 32; // devices per controller, at most
  static constexpr int MAXPORTS = 64;

  // registers (xHCI 5): capability, operational, runtime, doorbells.
  uint64 base;
  volatile uchar *op;
  volatile uchar *rt;
  volatile uint32 *db;
  int nports;
  int nslots;
  int csz; // context size: 32 or 64 bytes
  uchar portmajor[MAXPORTS + 1]; // 2 = USB 2 port, 3 = USB 3 port

  uint64 *dcbaa;    // device context base address array (xHCI 6.1)
  Ring cmdring;     // command ring (xHCI 4.6)
  Trb *evring;      // event ring, one segment (xHCI 4.9.4)
  int evidx;
  uint32 evcycle;
  UsbDev *devs[MAXSLOTS + 1];
  UsbDev *rootdev[MAXPORTS + 1]; // device on each port, if any
  bool porthandled[MAXPORTS + 1]; // the port's device was looked at
  uint64 portpending;            // ports with a status change to handle

  // the command in flight, and its result (set by poll()).
  uint64 cmdtrb;
  volatile bool cmddone;
  volatile int cmdcc;
  volatile int cmdslot;

  uint32 r32(uint64 off) { return *(volatile uint32 *)(base + off); }
  void w32(uint64 off, uint32 v) { *(volatile uint32 *)(base + off) = v; }
  uint32 opr(int off) { return *(volatile uint32 *)(op + off); }
  void opw(int off, uint32 v) { *(volatile uint32 *)(op + off) = v; }
  void opw64(int off, uint64 v);
  void rtw64(int off, uint64 v);
  uint32 portsc(int port) { return opr(0x400 + 0x10 * (port - 1)); }
  void setportsc(int port, uint32 v) { opw(0x400 + 0x10 * (port - 1), v); }

  void handoff();
  void protocols();
  bool reset();
  bool resetport(int port);
  void portchange(int port);
  int command(uint64 param, uint32 status, uint32 control);
  void resetep(UsbDev &d, int dci, Ring &ring);
  void event(Trb *e);
  uint32 *ictx(UsbDev &d, int i) { return (uint32 *)(d.inctx + i * csz); }
  uint32 *octx(UsbDev &d, int i) { return (uint32 *)(d.outctx + i * csz); }

  // wait up to ms milliseconds for done() to be true, handling
  // events meanwhile.
  template <typename F>
  bool wait(F done, int ms)
  {
    uint64 end = rdtsc() + (uint64)ms * 1000 * 5000; // see microdelay()
    while (!done()) {
      poll();
      if (rdtsc() > end)
        return false;
      asm volatile("pause");
    }
    return true;
  }
};

// usb.cpp
void usbattach(Xhci &hc, int speed, int rootport, uint32 route, int depth,
               int ttslot, int ttport, UsbDev **out);
