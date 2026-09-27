//
// a disk that is really memory: the file system image that the
// UEFI loader read into RAM. it takes the place of RISC-V xv6's
// virtio_disk.c, and works the same way on qemu, VirtualBox, and
// real PCs. changes are lost when the machine turns off.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "bootinfo.h"
#include "defs.h"

extern struct bootinfo bootinfo;

static char *disk;

void
ramdisk_init(void)
{
  if (bootinfo.fsimg == 0)
    panic("ramdisk: no fs.img");
  if (bootinfo.fsimg + FSSIZE * BSIZE > PHYSTOP)
    panic("ramdisk: fs.img above PHYSTOP");
  if (bootinfo.fsimg_size < FSSIZE * BSIZE)
    panic("ramdisk: fs.img too small");
  disk = (char *)bootinfo.fsimg;
}

// read or write buffer b. b must be locked.
// unlike a real disk this finishes at once, so there is no
// need to sleep waiting for an interrupt.
void
ramdisk_rw(struct buf *b, int write)
{
  if (b->blockno >= FSSIZE)
    panic("ramdisk_rw: blockno");

  char *addr = disk + b->blockno * BSIZE;
  if (write)
    memmove(addr, b->data, BSIZE);
  else
    memmove(b->data, addr, BSIZE);
}
