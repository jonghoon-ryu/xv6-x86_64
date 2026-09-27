// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "x86.h"
#include "bootinfo.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

extern struct bootinfo bootinfo;

// a PC's RAM has holes, and UEFI has put things in some of it,
// so free only the pages that the UEFI memory map says are
// unused, between the end of the kernel and PHYSTOP.
// (RISC-V xv6 frees everything from end to PHYSTOP.)
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  for (uint64 off = 0; off < bootinfo.memmap_size;
       off += bootinfo.memmap_descsize) {
    struct efi_memdesc *d = (struct efi_memdesc *)(bootinfo.memmap + off);
    if (d->type != EFI_CONVENTIONAL_MEMORY)
      continue;
    uint64 start = d->phys_start;
    uint64 stop = d->phys_start + d->npages * PGSIZE;
    if (start < (uint64)end)
      start = (uint64)end;
    if (stop > PHYSTOP)
      stop = PHYSTOP;
    if (start < stop)
      freerange((void *)start, (void *)stop);
  }
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}
