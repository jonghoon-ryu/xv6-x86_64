//
// PCI configuration space, through the PC's two configuration
// ports (0xCF8 = address, 0xCFC = data). every PC chipset still
// answers them for the first 256 bytes of each function's
// configuration space, which is all xv6 needs.
//
// not in the C version: xv6 finds its devices at fixed addresses
// (or through ACPI), but a USB controller sits wherever the
// firmware put it on the PCI bus.
//

#include "types.h"
#include "param.h"
#include "x86.h"
#include "defs.h"

constexpr ushort PCI_ADDR = 0xCF8;
constexpr ushort PCI_DATA = 0xCFC;

static uint32
pciaddr(int bus, int dev, int func, int off)
{
  return 0x80000000u | (bus << 16) | (dev << 11) | (func << 8) | (off & 0xFC);
}

uint32
pciread(int bus, int dev, int func, int off)
{
  outl(PCI_ADDR, pciaddr(bus, dev, func, off));
  return inl(PCI_DATA);
}

void
pciwrite(int bus, int dev, int func, int off, uint32 v)
{
  outl(PCI_ADDR, pciaddr(bus, dev, func, off));
  outl(PCI_DATA, v);
}

// call found(bus, dev, func) for every PCI function whose class
// code (register 0x08, bits 31:8) is cls. scans every bus number
// instead of following bridges: slower (8192 probes) but simpler.
void
pciscan(uint32 cls, void (*found)(int bus, int dev, int func))
{
  for (int bus = 0; bus < 256; bus++) {
    for (int dev = 0; dev < 32; dev++) {
      // a missing device reads all ones.
      if ((pciread(bus, dev, 0, 0x00) & 0xFFFF) == 0xFFFF)
        continue;
      // header type bit 7: the device has functions 1-7 too.
      int nfunc = (pciread(bus, dev, 0, 0x0C) & 0x800000) ? 8 : 1;
      for (int func = 0; func < nfunc; func++) {
        if ((pciread(bus, dev, func, 0x00) & 0xFFFF) == 0xFFFF)
          continue;
        if ((pciread(bus, dev, func, 0x08) >> 8) == cls)
          found(bus, dev, func);
      }
    }
  }
}
