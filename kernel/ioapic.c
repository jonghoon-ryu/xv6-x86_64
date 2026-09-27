// The I/O APIC manages hardware interrupts for an SMP system,
// the way RISC-V's PLIC routes device interrupts to harts.
// http://www.intel.com/design/chipsets/datashts/29056601.pdf
// See also picirq.c in the x86 version of xv6.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "defs.h"

#define REG_ID    0x00 // Register index: ID
#define REG_VER   0x01 // Register index: version
#define REG_TABLE 0x10 // Redirection table base

// The redirection table starts at REG_TABLE and uses
// two registers to configure each interrupt.
// The first (low) register in a pair contains configuration bits.
// The second (high) register contains a bitmask telling which
// CPUs can serve that interrupt.
#define INT_DISABLED  0x00010000 // Interrupt disabled
#define INT_LEVEL     0x00008000 // Level-triggered (vs edge-)
#define INT_ACTIVELOW 0x00002000 // Active low (vs high)
#define INT_LOGICAL   0x00000800 // Destination is CPU id (vs APIC ID)

extern uint64 ioapicaddr; // acpi.c
extern uchar apicids[];   // acpi.c

// IO APIC MMIO structure: write reg, then read or write data.
struct ioapic {
  uint reg;
  uint pad[3];
  uint data;
};

static volatile struct ioapic *ioapic;

static uint
ioapicread(int reg)
{
  ioapic->reg = reg;
  return ioapic->data;
}

static void
ioapicwrite(int reg, uint data)
{
  ioapic->reg = reg;
  ioapic->data = data;
}

void
ioapicinit(void)
{
  // turn off the legacy 8259 interrupt controllers,
  // which UEFI may have left enabled.
  outb(0x21, 0xFF);
  outb(0xA1, 0xFF);

  ioapic = (volatile struct ioapic *)ioapicaddr;
  int maxintr = (ioapicread(REG_VER) >> 16) & 0xFF;

  // Mark all interrupts edge-triggered, active high, disabled,
  // and not routed to any CPUs.
  for (int i = 0; i <= maxintr; i++) {
    ioapicwrite(REG_TABLE + 2 * i, INT_DISABLED | (T_IRQ0 + i));
    ioapicwrite(REG_TABLE + 2 * i + 1, 0);
  }
}

// route ISA interrupt irq to CPU cpu as vector T_IRQ0 + irq.
void
ioapicenable(int irq, int cpu)
{
  int gsi = acpi_isairq(irq);
  // Mark interrupt edge-triggered, active high,
  // enabled, and routed to the given cpu's APIC ID.
  ioapicwrite(REG_TABLE + 2 * gsi, T_IRQ0 + irq);
  ioapicwrite(REG_TABLE + 2 * gsi + 1, apicids[cpu] << 24);
}
