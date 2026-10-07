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

  // step 7 only: one command that does nothing, to see that the
  // command ring, the doorbell and the event ring all work.
  if (command(0, 0, TRB_NOOP_COMMAND << 10) == CC_SUCCESS)
    printk("xhci%d: running: a No Op command came back\n", id);
  else
    printk("xhci%d: the No Op command failed\n", id);
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
    if (porthandled[port])
      printk("xhci%d: port %d: disconnected\n", id, port);
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
  // step 7: say what is there; step 8 will set it up.
  static const char *names[] = { "?", "full", "low", "high", "super" };
  printk("xhci%d: port %d: a USB 2 device, %s speed\n", id, port,
         speed <= 4 ? names[speed] : "?");
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
