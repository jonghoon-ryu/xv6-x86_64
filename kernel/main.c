#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "bootinfo.h"
#include "defs.h"

volatile static int started = 0;

static void startothers(void);

// start() jumps here on the first CPU, mpenter() on the others.
void
main()
{
  if (cpuid() == 0) {
    consoleinit();
    printkinit();
    printk("\n");
    printk("xv6 kernel is booting\n");
    printk("\n");
    kinit();            // physical page allocator
    acpiinit();         // find CPUs and interrupt controllers
    kvminit();          // create kernel page table
    kvminithart();      // turn on paging
    procinit();         // process table
    trapinit();         // trap vectors
    trapinithart();     // install kernel trap vector
    ioapicinit();       // set up interrupt controller
    lapicinit();        // this CPU's interrupt controller and timer
    acpi_bspfirst(lapicid()); // make this CPU cpu 0
    ioapicenable(IRQ_COM1, 0); // ask for serial port interrupts
    binit();            // buffer cache
    iinit();            // inode table
    fileinit();         // file table
    ramdisk_init();     // file system image in memory
    userinit();         // first user process
    startothers();      // start other CPUs

    __atomic_store_n(&started, 1, __ATOMIC_RELEASE);
  } else {
    while (__atomic_load_n(&started, __ATOMIC_ACQUIRE) == 0)
      ;

    printk("hart %d starting\n", cpuid());
    kvminithart();  // turn on paging
    trapinithart(); // install kernel trap vector
    lapicinit();    // this CPU's interrupt controller and timer
  }

  scheduler();
}

// start the other CPUs, one at a time. qemu starts all RISC-V
// harts at boot; on a PC the boot CPU must wake the others.
static void
startothers(void)
{
  extern struct bootinfo bootinfo;
  extern pagetable_t kernel_pagetable;
  extern char stack0[];
  extern volatile int apstarted;
  extern int ncpu;
  extern uchar apicids[];
  extern char entryother_start[], entryother_end[], eo_gdt[], eo_gdtdesc[],
      eo_far32[], eo_far64[], eo_start32[], eo_start64[], eo_cr3[],
      eo_stack[], eo_id[], eo_entry[];
  void mpenter(uint64);

#define OFF(sym) ((uint64)(sym) - (uint64)entryother_start)

  // copy entryother.S's code to the low page the loader set aside,
  // and fill in the addresses it needs.
  uint64 base = bootinfo.apboot;
  char *code = (char *)base;
  memmove(code, entryother_start, entryother_end - entryother_start);
  *(uint *)(code + OFF(eo_gdtdesc) + 2) = base + OFF(eo_gdt);
  *(uint *)(code + OFF(eo_far32)) = base + OFF(eo_start32);
  *(uint *)(code + OFF(eo_far64)) = base + OFF(eo_start64);
  *(uint64 *)(code + OFF(eo_cr3)) = MAKE_CR3(kernel_pagetable);
  *(uint64 *)(code + OFF(eo_entry)) = (uint64)mpenter;

  for (int i = 1; i < ncpu; i++) {
    *(uint64 *)(code + OFF(eo_stack)) = (uint64)stack0 + 4096 * (i + 1);
    *(uint64 *)(code + OFF(eo_id)) = i;
    apstarted = 0;
    lapicstartap(apicids[i], base);

    // wait for the CPU to reach mpenter(), which means
    // it is done with entryother's page.
    int t;
    for (t = 0; t < 1000000; t++) {
      if (__atomic_load_n(&apstarted, __ATOMIC_ACQUIRE))
        break;
      microdelay(1);
    }
    if (t == 1000000)
      printk("cpu %d did not start\n", i);
  }
}
