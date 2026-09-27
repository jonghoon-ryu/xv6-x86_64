//
// find the CPUs and interrupt controllers by reading ACPI tables.
//
// RISC-V xv6 knows where qemu's devices are (memlayout.h). a PC
// describes its interrupt controllers in the ACPI MADT ("APIC")
// table, reached from the RSDP that UEFI gives the loader.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "bootinfo.h"
#include "defs.h"

struct acpi_rsdp {
  char signature[8]; // "RSD PTR "
  uchar checksum;
  char oemid[6];
  uchar revision; // 2 or more: has xsdt
  uint rsdt;
  uint length;
  uint64 xsdt;
  uchar xchecksum;
  uchar rsvd[3];
} __attribute__((packed));

struct acpi_header {
  char signature[4];
  uint length;
  uchar revision;
  uchar checksum;
  char oemid[6];
  char oemtableid[8];
  uint oemrevision;
  uint creatorid;
  uint creatorrevision;
} __attribute__((packed));

struct acpi_madt {
  struct acpi_header h;
  uint lapic; // local APIC address
  uint flags;
  uchar entries[];
} __attribute__((packed));

#define MADT_LAPIC       0
#define MADT_IOAPIC      1
#define MADT_ISO         2 // interrupt source override
#define MADT_LAPIC_ADDR  5 // 64-bit local APIC address

extern struct bootinfo bootinfo;

uint64 lapicaddr;  // physical address of the local APIC
uint64 ioapicaddr; // physical address of the IOAPIC
int ncpu;
uchar apicids[NCPU]; // local APIC id of each CPU, by cpu id

// which IOAPIC input each ISA IRQ is wired to; usually the same.
static uint isairq[16];

static struct acpi_header *
findtable(struct acpi_rsdp *rsdp, char *sig)
{
  if (rsdp->revision >= 2 && rsdp->xsdt) {
    struct acpi_header *xsdt = (struct acpi_header *)rsdp->xsdt;
    int n = (xsdt->length - sizeof(*xsdt)) / 8;
    uint64 *entries = (uint64 *)(xsdt + 1);
    for (int i = 0; i < n; i++) {
      struct acpi_header *h = (struct acpi_header *)entries[i];
      if (memcmp(h->signature, sig, 4) == 0)
        return h;
    }
  } else {
    struct acpi_header *rsdt = (struct acpi_header *)(uint64)rsdp->rsdt;
    int n = (rsdt->length - sizeof(*rsdt)) / 4;
    uint *entries = (uint *)(rsdt + 1);
    for (int i = 0; i < n; i++) {
      struct acpi_header *h = (struct acpi_header *)(uint64)entries[i];
      if (memcmp(h->signature, sig, 4) == 0)
        return h;
    }
  }
  return 0;
}

// read the MADT. must run before kvminithart(), while the firmware's
// page table still maps all of memory, since ACPI tables may lie
// above PHYSTOP.
void
acpiinit(void)
{
  for (int i = 0; i < 16; i++)
    isairq[i] = i;

  struct acpi_rsdp *rsdp = (struct acpi_rsdp *)bootinfo.rsdp;
  if (rsdp == 0 || memcmp(rsdp->signature, "RSD PTR ", 8) != 0)
    panic("acpiinit: no RSDP");

  struct acpi_madt *madt = (struct acpi_madt *)findtable(rsdp, "APIC");
  if (madt == 0)
    panic("acpiinit: no MADT");

  lapicaddr = madt->lapic;
  uchar *p = madt->entries;
  uchar *e = (uchar *)madt + madt->h.length;
  while (p < e) {
    int type = p[0], len = p[1];
    if (type == MADT_LAPIC) {
      uint flags = *(uint *)(p + 4);
      if ((flags & 1) && ncpu < NCPU) // enabled
        apicids[ncpu++] = p[3];
    } else if (type == MADT_IOAPIC) {
      if (ioapicaddr == 0) // xv6 uses only the first IOAPIC
        ioapicaddr = *(uint *)(p + 4);
    } else if (type == MADT_ISO) {
      int source = p[3];
      uint gsi = *(uint *)(p + 4);
      if (source < 16)
        isairq[source] = gsi;
    } else if (type == MADT_LAPIC_ADDR) {
      lapicaddr = *(uint64 *)(p + 4);
    }
    if (len == 0)
      break;
    p += len;
  }

  if (ncpu == 0 || ioapicaddr == 0)
    panic("acpiinit: no CPUs or IOAPIC");
}

// put the boot CPU first in apicids[], since its cpu id is 0.
void
acpi_bspfirst(int apicid)
{
  for (int i = 0; i < ncpu; i++) {
    if (apicids[i] == apicid) {
      apicids[i] = apicids[0];
      apicids[0] = apicid;
      return;
    }
  }
}

// the IOAPIC input for an ISA IRQ.
int
acpi_isairq(int irq)
{
  return isairq[irq];
}
