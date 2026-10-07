//
// the USB 3 host controller (xHCI), for USB keyboards on real PCs.
//
// [platform: real PC] modern PCs have no PS/2 controller, and the
// firmware's PS/2 emulation for USB keyboards stops when it hands
// the machine to the kernel, so a USB keyboard needs this driver.
// [platform: QEMU] make qemu USB=1 adds an xHCI controller with a
// USB keyboard; without it, QEMU has no xHCI.
// [platform: VirtualBox] its xHCI needs the extension pack; the
// VM uses the PS/2 keyboard.
//
// the driver is found at run time, not chosen at compile time: if
// the PCI scan finds no xHCI controller, nothing here runs.
//
// there are no interrupts yet (steps 13-14), so main() calls
// usbintr() in its loop, which reads the controller's event ring
// (polling), like kbdintr() for the PS/2 keyboard.
//
// there is no page allocator yet either (kalloc.cpp comes in the
// memory part), so the controller's memory comes from dmapage(),
// a small allocator over the UEFI memory map.
//
// not in the C version.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "bootinfo.h"
#include "defs.h"
#include "xhci.h"

extern struct bootinfo bootinfo;
extern char end[]; // first address after kernel, from kernel.ld

// ---------------------------------------------------------------
// memory for the controller: whole pages, below 4 GB (some
// controllers cannot reach above), mapped at their physical
// address by the firmware's page tables like all of RAM.

static uint64 dmanext, dmaend; // the free region being used up
static uint64 dmafree;         // freed pages, linked through their first word

// find the next free (UEFI "conventional") region below 4 GB,
// starting at or above from. prefer memory above PHYSTOP, which
// kalloc.cpp will not use.
static bool
dmaregion(uint64 from)
{
  for (int pass = 0; pass < 2; pass++) {
    for (uint64 off = 0; off < bootinfo.memmap_size; off += bootinfo.memmap_descsize) {
      auto d = reinterpret_cast<efi_memdesc *>(bootinfo.memmap + off);
      uint64 s = d->phys_start, e = s + d->npages * PGSIZE;
      if (d->type != EFI_CONVENTIONAL_MEMORY)
        continue;
      if (pass == 0 && s < PHYSTOP)
        s = PHYSTOP;
      if (s < from)
        s = from;
      if (s < PGROUNDUP((uint64)end))
        s = PGROUNDUP((uint64)end);
      if (e > 0x100000000L)
        e = 0x100000000L;
      if (s + PGSIZE <= e) {
        dmanext = s;
        dmaend = e;
        return true;
      }
    }
  }
  return false;
}

// a zeroed page, or panic.
static void *
dmapage()
{
  void *p;
  if (dmafree) {
    p = (void *)dmafree;
    dmafree = *(uint64 *)dmafree;
  } else {
    if (dmanext + PGSIZE > dmaend && !dmaregion(dmaend))
      panic("xhci: out of memory");
    p = (void *)dmanext;
    dmanext += PGSIZE;
  }
  memset(p, 0, PGSIZE);
  return p;
}

static void
dmafreepage(void *p)
{
  *(uint64 *)p = dmafree;
  dmafree = (uint64)p;
}

// [platform: real PC] the kernel still runs on the firmware's page
// tables, which map RAM but need not map every device. check that
// an address is mapped before touching it, rather than crash.
static bool
ismapped(uint64 a)
{
  constexpr uint64 PTE_PS = 1L << 7; // a 2 MB or 1 GB page
  uint64 cr4;
  asm volatile("mov %%cr4, %0" : "=r"(cr4));
  int top = (cr4 & (1L << 12)) ? 4 : 3; // 5-level paging?
  auto t = (uint64 *)PTE2PA(r_cr3());
  for (int level = top; level >= 0; level--) {
    uint64 e = t[PX(level, a)];
    if ((e & PTE_V) == 0)
      return false;
    if (level == 0 || ((level == 1 || level == 2) && (e & PTE_PS)))
      return true;
    t = (uint64 *)PTE2PA(e);
  }
  return false;
}

// ---------------------------------------------------------------
// rings

void
Ring::init()
{
  trb = (Trb *)dmapage();
  idx = 0;
  cycle = 1;
}

// add a TRB; return where it went, so that its event can be
// recognized. the cycle bit is written last: until it matches the
// controller's cycle state, the controller ignores the TRB.
Trb *
Ring::push(uint64 param, uint32 status, uint32 control)
{
  Trb *t = &trb[idx];
  t->param = param;
  t->status = status;
  asm volatile("" ::: "memory");
  t->control = (control & ~1u) | cycle;
  if (++idx == NTRB - 1) {
    // the link TRB back to the start; Toggle Cycle (bit 1) makes
    // the controller flip its cycle state as it follows it.
    Trb *l = &trb[idx];
    l->param = (uint64)trb;
    l->status = 0;
    asm volatile("" ::: "memory");
    l->control = (TRB_LINK << 10) | (1 << 1) | cycle;
    idx = 0;
    cycle ^= 1;
  }
  return t;
}

// ---------------------------------------------------------------
// registers (xHCI 5)

// capability registers (offsets from base).
constexpr int CAP_LENGTH = 0x00;
constexpr int CAP_HCSPARAMS1 = 0x04;
constexpr int CAP_HCSPARAMS2 = 0x08;
constexpr int CAP_HCCPARAMS1 = 0x10;
constexpr int CAP_DBOFF = 0x14;
constexpr int CAP_RTSOFF = 0x18;

// operational registers (offsets from op).
constexpr int OP_USBCMD = 0x00;
constexpr int OP_USBSTS = 0x04;
constexpr int OP_PAGESIZE = 0x08;
constexpr int OP_CRCR = 0x18;
constexpr int OP_DCBAAP = 0x30;
constexpr int OP_CONFIG = 0x38;

constexpr uint32 CMD_RUN = 1 << 0;
constexpr uint32 CMD_HCRST = 1 << 1;
constexpr uint32 STS_HCH = 1 << 0;  // halted
constexpr uint32 STS_CNR = 1 << 11; // controller not ready

// interrupter 0's registers (offsets from rt).
constexpr int IR0_ERSTSZ = 0x28;
constexpr int IR0_ERSTBA = 0x30;
constexpr int IR0_ERDP = 0x38;
constexpr uint64 ERDP_EHB = 1 << 3; // event handler busy

// PORTSC bits (xHCI 5.4.8).
constexpr uint32 PORT_CCS = 1 << 0;  // a device is connected
constexpr uint32 PORT_PED = 1 << 1;  // enabled; writing 1 DISABLES the port
constexpr uint32 PORT_PR = 1 << 4;   // reset
constexpr uint32 PORT_PP = 1 << 9;   // power
constexpr uint32 PORT_CSC = 1 << 17; // connect status changed
constexpr uint32 PORT_PRC = 1 << 21; // reset finished
constexpr uint32 PORT_CHANGES = 0x7F << 17; // all the "changed" bits

// the PORTSC bits that keep their value when written back; all
// others either are read-only or do something when written as 1
// (Linux calls this xhci_port_state_to_neutral()).
static uint32
neutral(uint32 v)
{
  constexpr uint32 RO = (1 << 0) | (1 << 3) | (0xF << 10) | (1 << 30);
  constexpr uint32 RWS = (0xF << 5) | (1 << 9) | (3 << 14) | (7 << 25);
  return v & (RO | RWS);
}

// a 64-bit register written as two 32-bit halves, low first,
// which every controller accepts (xHCI 5.1).
void
Xhci::opw64(int off, uint64 v)
{
  opw(off, (uint32)v);
  opw(off + 4, (uint32)(v >> 32));
}

void
Xhci::rtw64(int off, uint64 v)
{
  *(volatile uint32 *)(rt + off) = (uint32)v;
  *(volatile uint32 *)(rt + off + 4) = (uint32)(v >> 32);
}

// ---------------------------------------------------------------
// controller setup (xHCI 4.2)

// [platform: real PC] the firmware drives the controller itself
// (for its own keyboard support), and may also have System
// Management Mode code watching it. ask the firmware to let go
// (xHCI 4.22.1, the USB Legacy Support capability).
void
Xhci::handoff()
{
  uint32 off = (r32(CAP_HCCPARAMS1) >> 16) << 2;
  while (off) {
    uint32 v = r32(off);
    if ((v & 0xFF) == 1) {
      constexpr uint32 BIOS_OWNED = 1 << 16, OS_OWNED = 1 << 24;
      if (v & BIOS_OWNED) {
        w32(off, v | OS_OWNED);
        if (!wait([&] { return (r32(off) & BIOS_OWNED) == 0; }, 1000)) {
          printk("xhci%d: firmware did not let go; taking over\n", id);
          w32(off, (r32(off) & ~BIOS_OWNED) | OS_OWNED);
        }
      }
      // turn off the firmware's SMIs, and clear their status bits.
      uint32 c = r32(off + 4);
      c &= ~((1 << 0) | (1 << 4) | (1 << 13) | (1 << 14) | (1 << 15));
      c |= 7u << 29;
      w32(off + 4, c);
    }
    uint32 next = (v >> 8) & 0xFF;
    off = next ? off + (next << 2) : 0;
  }
}

// which ports are USB 2 and which are USB 3: each physical USB 3
// socket appears as two controller ports, one per protocol (xHCI
// 7.2, the Supported Protocol capability).
void
Xhci::protocols()
{
  uint32 off = (r32(CAP_HCCPARAMS1) >> 16) << 2;
  while (off) {
    uint32 v = r32(off);
    if ((v & 0xFF) == 2) {
      int major = v >> 24;
      uint32 w = r32(off + 8);
      int first = w & 0xFF, count = (w >> 8) & 0xFF;
      for (int p = first; p < first + count && p <= MAXPORTS; p++)
        portmajor[p] = major;
    }
    uint32 next = (v >> 8) & 0xFF;
    off = next ? off + (next << 2) : 0;
  }
}

// stop the controller, then reset it to a known state.
bool
Xhci::reset()
{
  opw(OP_USBCMD, opr(OP_USBCMD) & ~CMD_RUN);
  if (!wait([&] { return opr(OP_USBSTS) & STS_HCH; }, 100))
    return false;
  opw(OP_USBCMD, CMD_HCRST);
  // [platform: real PC] some Intel controllers hang if their
  // registers are read too soon after a reset (Linux waits too).
  microdelay(1000);
  return wait([&] { return (opr(OP_USBCMD) & CMD_HCRST) == 0 &&
                           (opr(OP_USBSTS) & STS_CNR) == 0; },
              1000);
}

bool
Xhci::init(int bus, int dev, int func)
{
  // let the controller answer at its memory address (bit 1) and
  // reach memory itself (bus master, bit 2).
  pciwrite(bus, dev, func, 0x04, pciread(bus, dev, func, 0x04) | 0x6);

  // BAR0: the registers' address, maybe 64 bits wide.
  uint32 bar = pciread(bus, dev, func, 0x10);
  base = bar & ~0xFull;
  if ((bar & 0x6) == 0x4)
    base |= (uint64)pciread(bus, dev, func, 0x14) << 32;
  uint32 ids = pciread(bus, dev, func, 0x00);
  printk("xhci%d: PCI %d:%d.%d, vendor %x device %x, registers at %p\n", id,
         bus, dev, func, ids & 0xFFFF, ids >> 16, (void *)base);
  if (!ismapped(base)) {
    printk("xhci%d: registers not mapped by the firmware; skipped\n", id);
    return false;
  }

  op = (volatile uchar *)(base + (r32(CAP_LENGTH) & 0xFF));
  rt = (volatile uchar *)(base + (r32(CAP_RTSOFF) & ~0x1Fu));
  db = (volatile uint32 *)(base + (r32(CAP_DBOFF) & ~0x3u));
  uint32 hcs1 = r32(CAP_HCSPARAMS1), hcs2 = r32(CAP_HCSPARAMS2);
  nslots = hcs1 & 0xFF;
  if (nslots > MAXSLOTS)
    nslots = MAXSLOTS;
  nports = hcs1 >> 24;
  if (nports > MAXPORTS)
    nports = MAXPORTS;
  csz = (r32(CAP_HCCPARAMS1) & (1 << 2)) ? 64 : 32;
  int nscratch = ((hcs2 >> 21) & 0x1F) << 5 | (hcs2 >> 27);
  printk("xhci%d: version %x, %d ports, %d slots, %d-byte contexts, "
         "%d scratchpad pages\n",
         id, r32(CAP_LENGTH) >> 16, nports, nslots, csz, nscratch);
  if (!ismapped((uint64)db) || !ismapped((uint64)rt)) {
    printk("xhci%d: registers not mapped by the firmware; skipped\n", id);
    return false;
  }

  handoff();
  protocols();
  if (!reset()) {
    printk("xhci%d: reset failed; skipped\n", id);
    return false;
  }
  if ((opr(OP_PAGESIZE) & 1) == 0) {
    printk("xhci%d: no 4 KB pages; skipped\n", id);
    return false;
  }

  opw(OP_CONFIG, nslots);

  // the device context base address array: one pointer per slot.
  // entry 0 points to scratchpad pages, memory the controller
  // keeps for itself (xHCI 4.20).
  dcbaa = (uint64 *)dmapage();
  if (nscratch > 0) {
    if (nscratch > 512)
      panic("xhci: too many scratchpad pages");
    auto sp = (uint64 *)dmapage();
    for (int i = 0; i < nscratch; i++)
      sp[i] = (uint64)dmapage();
    dcbaa[0] = (uint64)sp;
  }
  opw64(OP_DCBAAP, (uint64)dcbaa);

  cmdring.init();
  opw64(OP_CRCR, (uint64)cmdring.trb | 1);

  // the event ring: one segment, described by a one-entry
  // segment table (xHCI 6.5).
  evring = (Trb *)dmapage();
  evidx = 0;
  evcycle = 1;
  auto erst = (uint64 *)dmapage();
  erst[0] = (uint64)evring;
  erst[1] = Ring::NTRB;
  *(volatile uint32 *)(rt + IR0_ERSTSZ) = 1;
  rtw64(IR0_ERDP, (uint64)evring);
  rtw64(IR0_ERSTBA, (uint64)erst);

  opw(OP_USBCMD, CMD_RUN);
  if (!wait([&] { return (opr(OP_USBSTS) & STS_HCH) == 0; }, 100)) {
    printk("xhci%d: does not start; skipped\n", id);
    return false;
  }

  // power the ports, if software controls port power, and look at
  // every port once; service() then enumerates what is connected.
  for (int p = 1; p <= nports; p++) {
    uint32 v = portsc(p);
    if ((v & PORT_PP) == 0)
      setportsc(p, neutral(v) | PORT_PP);
    portpending |= 1ull << (p - 1);
  }
  microdelay(20 * 1000);
  return true;
}

// ---------------------------------------------------------------
// events and commands

// read every new event on the event ring.
void
Xhci::poll()
{
  // [platform: real PC] wait() polls during handoff() and reset(),
  // before the event ring exists. reading through the null pointer
  // happened to work in QEMU, but firmware that unmaps page 0 (to
  // catch null pointers) would fault, and with no IDT, reboot.
  if (evring == nullptr)
    return;
  bool any = false;
  for (;;) {
    Trb *e = &evring[evidx];
    if ((e->control & 1) != evcycle)
      break;
    event(e);
    any = true;
    if (++evidx == Ring::NTRB) {
      evidx = 0;
      evcycle ^= 1;
    }
  }
  // tell the controller how far software has read.
  if (any)
    rtw64(IR0_ERDP, (uint64)&evring[evidx] | ERDP_EHB);
}

void
Xhci::event(Trb *e)
{
  uint32 type = (e->control >> 10) & 0x3F;
  int cc = e->status >> 24;
  int slot = e->control >> 24;

  if (type == TRB_COMMAND_COMPLETION) {
    if (e->param == cmdtrb) {
      cmdcc = cc;
      cmdslot = slot;
      cmddone = true;
    }
  } else if (type == TRB_PORT_STATUS_CHANGE) {
    int port = (e->param >> 24) & 0xFF;
    if (port >= 1 && port <= nports)
      portpending |= 1ull << (port - 1);
  } else if (type == TRB_TRANSFER_EVENT) {
    int dci = (e->control >> 16) & 0x1F;
    UsbDev *d = slot <= nslots ? devs[slot] : nullptr;
    if (d == nullptr)
      return;
    if (dci == 1) {
      // a control transfer: the status stage finished (success),
      // or some stage failed. a short data stage is not an end.
      if (cc != CC_SHORT_PACKET) {
        d->ctlcc = cc;
        d->ctldone = true;
      }
    } else if (d->kbd && dci == d->kbddci) {
      if (cc == CC_SUCCESS || cc == CC_SHORT_PACKET) {
        usbkbdreport(*d, d->kbdmps - (e->status & 0xFFFFFF));
        queuein(*d);
      } else {
        printk("usb: keyboard error %d; keyboard stopped\n", cc);
      }
    }
  }
}

// run one command and wait for it; return its completion code,
// or -1 if the controller did not answer.
int
Xhci::command(uint64 param, uint32 status, uint32 control)
{
  Trb *t = cmdring.push(param, status, control);
  cmdtrb = (uint64)t;
  cmddone = false;
  db[0] = 0; // ring the command doorbell
  if (!wait([&] { return cmddone; }, 2000)) {
    printk("xhci%d: command %d timed out\n", id, (control >> 10) & 0x3F);
    return -1;
  }
  return cmdcc;
}

// ---------------------------------------------------------------
// ports

// reset a USB 2 port, which enables it (xHCI 4.3.1). USB 3 ports
// enable themselves when a device connects.
bool
Xhci::resetport(int port)
{
  setportsc(port, neutral(portsc(port)) | PORT_PR);
  if (!wait([&] { return portsc(port) & PORT_PRC; }, 500))
    return false;
  setportsc(port, neutral(portsc(port)) | PORT_PRC);
  microdelay(10 * 1000); // reset recovery time (USB 2.0 7.1.7.5)
  return portsc(port) & PORT_PED;
}

void
Xhci::portchange(int port)
{
  uint32 v = portsc(port);
  setportsc(port, neutral(v) | (v & PORT_CHANGES)); // acknowledge changes

  if ((v & PORT_CCS) == 0) {
    if (rootdev[port])
      printk("usb: device on port %d disconnected\n", port);
    // the device, and anything on it if it was a hub.
    for (int s = 1; s <= nslots; s++)
      if (devs[s] && devs[s]->rootport == port)
        freedev(devs[s]);
    rootdev[port] = nullptr;
    porthandled[port] = false;
    return;
  }
  // handled already: set up, ignored, or failed. resetting a port
  // makes the controller report a change on it again, so without
  // this an ignored device would be set up over and over (seen on
  // a real PC: a built-in MSI device, id db0:76). a device gets
  // one try per connection; unplug and replug to try again.
  if (porthandled[port])
    return;
  porthandled[port] = true;

  // [platform: real PC] keyboards are USB 2 (low or full speed),
  // so USB 3 devices are left alone.
  if (portmajor[port] == 3)
    return;
  microdelay(100 * 1000); // let the connection settle (USB 2.0 7.1.7.3)
  if ((portsc(port) & PORT_CCS) == 0)
    return;
  if (!resetport(port)) {
    printk("usb: port %d does not enable\n", port);
    return;
  }
  int speed = (portsc(port) >> 10) & 0xF;
  usbattach(*this, speed, port, 0, 0, 0, 0, &rootdev[port]);
}

// set up newly connected devices, forget disconnected ones.
void
Xhci::service()
{
  poll();
  while (portpending) {
    int p = __builtin_ctzll(portpending) + 1;
    portpending &= ~(1ull << (p - 1));
    portchange(p);
  }
}

// ---------------------------------------------------------------
// devices (xHCI 4.3)

// a new device: a slot from the controller, and its memory.
UsbDev *
Xhci::newdev(int speed, int rootport, uint32 route, int depth, int ttslot,
             int ttport)
{
  if (command(0, 0, TRB_ENABLE_SLOT << 10) != CC_SUCCESS)
    return nullptr;
  int slot = cmdslot;
  if (slot < 1 || slot > nslots) {
    printk("xhci%d: slot %d out of range\n", id, slot);
    return nullptr;
  }

  auto d = (UsbDev *)dmapage(); // zeroed; one page is plenty
  d->hc = this;
  d->slot = slot;
  d->speed = speed;
  d->rootport = rootport;
  d->route = route;
  d->depth = depth;
  d->ttslot = ttslot;
  d->ttport = ttport;
  // the largest packet endpoint 0 surely accepts; for full speed
  // the real size is in the device descriptor (see usbattach()).
  d->mps0 = speed == SPEED_SUPER ? 512 : speed == SPEED_HIGH ? 64 : 8;
  d->ep0.init();
  d->inctx = (uchar *)dmapage();
  d->outctx = (uchar *)dmapage();
  d->buf = (uchar *)dmapage();
  dcbaa[slot] = (uint64)d->outctx;
  devs[slot] = d;
  return d;
}

void
Xhci::freedev(UsbDev *d)
{
  command(0, 0, (TRB_DISABLE_SLOT << 10) | (d->slot << 24));
  devs[d->slot] = nullptr;
  dcbaa[d->slot] = 0;
  dmafreepage(d->ep0.trb);
  dmafreepage(d->inctx);
  dmafreepage(d->outctx);
  dmafreepage(d->buf);
  if (d->kbd)
    dmafreepage(d->kbdring.trb);
  dmafreepage(d);
}

// fill in the input context's endpoint 0 (xHCI 6.2.3).
static void
ep0ctx(uint32 *ep, UsbDev &d)
{
  ep[1] = (3 << 1) | (4 << 3) | (d.mps0 << 16); // 3 retries, type Control
  uint64 deq = (uint64)d.ep0.trb | 1;
  ep[2] = (uint32)deq;
  ep[3] = (uint32)(deq >> 32);
  ep[4] = 8; // average TRB length
}

// give the device its address (xHCI 4.3.4): the controller sends
// it SET_ADDRESS, and starts endpoint 0.
bool
Xhci::addressdevice(UsbDev &d)
{
  memset(d.inctx, 0, PGSIZE);
  ictx(d, 0)[1] = (1 << 0) | (1 << 1); // add the slot and endpoint 0
  uint32 *s = ictx(d, 1);
  s[0] = d.route | (d.speed << 20) | (1 << 27); // 1 context entry
  s[1] = d.rootport << 16;
  s[2] = d.ttslot | (d.ttport << 8);
  ep0ctx(ictx(d, 2), d);
  int cc = command((uint64)d.inctx, 0, (TRB_ADDRESS_DEVICE << 10) | (d.slot << 24));
  if (cc != CC_SUCCESS)
    printk("usb: address device failed (%d)\n", cc);
  return cc == CC_SUCCESS;
}

// tell the controller endpoint 0's real packet size (xHCI 4.6.7).
bool
Xhci::setmps0(UsbDev &d)
{
  memset(d.inctx, 0, PGSIZE);
  ictx(d, 0)[1] = 1 << 1; // endpoint 0
  ep0ctx(ictx(d, 2), d);
  return command((uint64)d.inctx, 0,
                 (TRB_EVALUATE_CONTEXT << 10) | (d.slot << 24)) == CC_SUCCESS;
}

// after a STALL the endpoint is halted: restart it, and continue
// after the failed TRBs (xHCI 4.6.8, 4.6.10).
void
Xhci::resetep(UsbDev &d, int dci, Ring &ring)
{
  command(0, 0, (TRB_RESET_EP << 10) | (dci << 16) | (d.slot << 24));
  command(ring.dequeue(), 0,
          (TRB_SET_TR_DEQUEUE << 10) | (dci << 16) | (d.slot << 24));
}

// a control transfer on endpoint 0 (xHCI 4.11.2.2): a setup
// stage, an optional data stage of len bytes, and a status stage.
bool
Xhci::control(UsbDev &d, uchar reqtype, uchar req, ushort value, ushort index,
              ushort len, void *data)
{
  constexpr uint32 IDT = 1 << 6, IOC = 1 << 5, DIR_IN = 1 << 16;
  bool in = reqtype & 0x80;
  if (len > PGSIZE)
    return false;

  uint64 setup = reqtype | (req << 8) | ((uint64)value << 16) |
                 ((uint64)index << 32) | ((uint64)len << 48);
  uint32 trt = len == 0 ? 0 : in ? 3 : 2; // transfer type: none, OUT, IN
  d.ep0.push(setup, 8, (TRB_SETUP << 10) | IDT | (trt << 16));
  if (len > 0) {
    if (!in)
      memmove(d.buf, data, len);
    d.ep0.push((uint64)d.buf, len, (TRB_DATA << 10) | (in ? DIR_IN : 0));
  }
  // the status stage goes the other way from the data.
  d.ep0.push(0, 0, (TRB_STATUS << 10) | IOC | (len > 0 && in ? 0 : DIR_IN));

  d.ctldone = false;
  db[d.slot] = 1; // endpoint 0's doorbell
  if (!wait([&] { return d.ctldone; }, 1000)) {
    printk("usb: request %x timed out\n", req);
    return false;
  }
  if (d.ctlcc != CC_SUCCESS) {
    // a STALL is the device saying "not supported".
    resetep(d, 1, d.ep0);
    return false;
  }
  if (in && len > 0)
    memmove(data, d.buf, len);
  return true;
}

// add an interrupt or bulk endpoint (xHCI 4.6.6). interval is in
// the controller's units: 2^interval x 125 microseconds.
bool
Xhci::configep(UsbDev &d, int dci, int type, int mps, int interval)
{
  memset(d.inctx, 0, PGSIZE);
  ictx(d, 0)[1] = (1 << 0) | (1 << dci);
  uint32 *s = ictx(d, 1);
  memmove(s, octx(d, 0), csz); // the slot context as it is now
  uint32 entries = s[0] >> 27;
  if ((uint32)dci > entries)
    entries = dci;
  s[0] = (s[0] & ~(0x1Fu << 27)) | (entries << 27);
  s[3] = 0;

  Ring &r = d.kbdring;
  r.init();
  uint32 *ep = ictx(d, dci + 1);
  ep[0] = interval << 16;
  ep[1] = (3 << 1) | (type << 3) | (mps << 16);
  uint64 deq = (uint64)r.trb | 1;
  ep[2] = (uint32)deq;
  ep[3] = (uint32)(deq >> 32);
  ep[4] = mps | (mps << 16); // average TRB length; max payload per interval
  int cc = command((uint64)d.inctx, 0, (TRB_CONFIGURE_EP << 10) | (d.slot << 24));
  if (cc != CC_SUCCESS)
    printk("usb: configure endpoint failed (%d)\n", cc);
  return cc == CC_SUCCESS;
}

// ask the keyboard for its next report. the device answers only
// when a key goes down or up; until then the controller keeps
// asking it, without the CPU.
void
Xhci::queuein(UsbDev &d)
{
  constexpr uint32 ISP = 1 << 2, IOC = 1 << 5;
  d.kbdring.push((uint64)d.report, d.kbdmps, (TRB_NORMAL << 10) | ISP | IOC);
  db[d.slot] = d.kbddci;
}

// ---------------------------------------------------------------
// the rest of the kernel sees only these.

static Xhci xhci[4];
static int nxhci;


static void
found(int bus, int dev, int func)
{
  if (nxhci == 4)
    return;
  Xhci &hc = xhci[nxhci];
  hc.id = nxhci;
  if (hc.init(bus, dev, func))
    nxhci++;
}

// find and start every xHCI controller, and the keyboards on them.
void
usbinit()
{
  if (!dmaregion(0)) {
    printk("usb: no memory below 4 GB\n");
    return;
  }
  pciscan(0x0C0330, found); // class: serial bus, USB, xHCI
  if (nxhci == 0)
    printk("usb: no xHCI controller\n");
  for (int i = 0; i < nxhci; i++)
    xhci[i].service();
}

// handle key reports, and devices plugged in or out.
void
usbintr()
{
  for (int i = 0; i < nxhci; i++)
    xhci[i].service();
}
