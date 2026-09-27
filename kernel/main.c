#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "defs.h"

volatile static int started = 0;

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
    ioapicenable(IRQ_COM1, 0); // ask for serial port interrupts
    binit();            // buffer cache
    iinit();            // inode table
    fileinit();         // file table
    ramdisk_init();     // file system image in memory
    userinit();         // first user process

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
