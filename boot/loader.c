//
// the xv6 UEFI loader: EFI/BOOT/BOOTX64.EFI on the boot partition.
//
// UEFI firmware runs it in 64-bit mode with all memory mapped at
// its physical address. it reads the kernel and the file system
// image from the boot partition, gathers what the kernel needs to
// know about the machine into struct bootinfo, leaves the
// firmware's boot services, and jumps to the kernel's _entry.
//
// it plays the part of qemu's -kernel option for RISC-V xv6, and
// of bootasm.S/bootmain.c in the old x86 xv6.
//
// the same loader runs under QEMU (OVMF firmware), VirtualBox (its
// EFI firmware), and on a real PC. comments marked [platform: ...]
// explain code that is there because of one of them.
//

#include "efi.h"
#include "../kernel/bootinfo.h"

// must match PHYSTOP in kernel/memlayout.h: the kernel can only
// use memory below it.
#define PHYSTOP (128ULL * 1024 * 1024)
#define PGSIZE  4096ULL

// the kernel's _entry uses the System V calling convention,
// not the Microsoft one that UEFI code uses.
typedef void __attribute__((sysv_abi)) (*kernel_entry)(struct bootinfo *);

// ELF64 file format, as in kernel/elf.h.
struct elfhdr {
  UINT32 magic;
  UINT8 elf[12];
  UINT16 type;
  UINT16 machine;
  UINT32 version;
  UINT64 entry;
  UINT64 phoff;
  UINT64 shoff;
  UINT32 flags;
  UINT16 ehsize;
  UINT16 phentsize;
  UINT16 phnum;
  UINT16 shentsize;
  UINT16 shnum;
  UINT16 shstrndx;
};

struct proghdr {
  UINT32 type;
  UINT32 flags;
  UINT64 off;
  UINT64 vaddr;
  UINT64 paddr;
  UINT64 filesz;
  UINT64 memsz;
  UINT64 align;
};

#define ELF_MAGIC    0x464C457FU
#define ELF_PROG_LOAD 1

static EFI_SYSTEM_TABLE *ST;
static EFI_BOOT_SERVICES *BS;

// clang may call these for structure copies.
void *
memset(void *dst, int c, UINTN n)
{
  UINT8 *d = dst;
  while (n-- > 0)
    *d++ = c;
  return dst;
}

void *
memcpy(void *dst, const void *src, UINTN n)
{
  UINT8 *d = dst;
  const UINT8 *s = src;
  while (n-- > 0)
    *d++ = *s++;
  return dst;
}

static int
guideq(EFI_GUID *a, EFI_GUID *b)
{
  UINT8 *x = (UINT8 *)a, *y = (UINT8 *)b;
  for (int i = 0; i < sizeof(EFI_GUID); i++)
    if (x[i] != y[i])
      return 0;
  return 1;
}

static void
print(CHAR16 *s)
{
  ST->ConOut->OutputString(ST->ConOut, s);
}

static void
printhex(UINT64 x)
{
  CHAR16 buf[19];
  buf[0] = '0';
  buf[1] = 'x';
  for (int i = 0; i < 16; i++) {
    int d = (x >> (60 - 4 * i)) & 0xf;
    buf[2 + i] = d < 10 ? '0' + d : 'a' + d - 10;
  }
  buf[18] = 0;
  print(buf);
}

static void
printdec(UINT64 x)
{
  CHAR16 buf[21];
  int i = 20;
  buf[i] = 0;
  do {
    buf[--i] = '0' + x % 10;
    x /= 10;
  } while (x != 0);
  print(&buf[i]);
}

static void
fail(CHAR16 *msg, EFI_STATUS status)
{
  print(L"xv6 loader: ");
  print(msg);
  print(L" ");
  printhex(status);
  print(L"\r\n");
  for (;;)
    ;
}

// allocate npages pages of memory below PHYSTOP.
static UINT64
lowpages(UINTN npages, UINT64 max)
{
  UINT64 addr = max - 1;
  EFI_STATUS s = BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, npages,
                                   &addr);
  if (EFI_ERROR(s))
    fail(L"out of memory", s);
  return addr;
}

// open a file in the boot partition's root directory,
// and return its size.
static EFI_FILE *
openfile(EFI_FILE *root, CHAR16 *name, UINT64 *size)
{
  EFI_FILE *f;
  EFI_STATUS s = root->Open(root, &f, name, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(s)) {
    print(name);
    fail(L": cannot open", s);
  }
  // find the size by seeking to the end.
  f->SetPosition(f, 0xFFFFFFFFFFFFFFFFULL);
  f->GetPosition(f, size);
  f->SetPosition(f, 0);
  return f;
}

static void
readfile(EFI_FILE *f, void *buf, UINT64 size)
{
  UINTN n = size;
  EFI_STATUS s = f->Read(f, &n, buf);
  if (EFI_ERROR(s) || n != size)
    fail(L"read failed", s);
}

// print the firmware's memory map entries that overlap [lo, hi),
// to see who owns the memory the kernel needs.
static void
showmemmap(UINT64 lo, UINT64 hi)
{
  static UINT8 map[16 * PGSIZE];
  UINTN mapsize = sizeof(map), mapkey, descsize;
  UINT32 descversion;
  if (EFI_ERROR(BS->GetMemoryMap(&mapsize, (EFI_MEMORY_DESCRIPTOR *)map,
                                 &mapkey, &descsize, &descversion)))
    return;
  print(L"  memory map near the kernel (type 7 = free):\r\n");
  for (UINTN off = 0; off < mapsize; off += descsize) {
    EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)(map + off);
    UINT64 end = d->PhysicalStart + d->NumberOfPages * PGSIZE;
    if (end <= lo || d->PhysicalStart >= hi)
      continue;
    print(L"    ");
    printhex(d->PhysicalStart);
    print(L" - ");
    printhex(end);
    print(L" type ");
    printdec(d->Type);
    print(L"\r\n");
  }
}

// copy the kernel's segments to the physical addresses it was
// linked for, and return its entry point.
static UINT64
loadkernel(EFI_FILE *root)
{
  UINT64 size;
  void *image;
  EFI_FILE *f = openfile(root, L"\\kernel", &size);
  EFI_STATUS s = BS->AllocatePool(EfiLoaderData, size, &image);
  if (EFI_ERROR(s))
    fail(L"out of memory", s);
  readfile(f, image, size);
  f->Close(f);

  struct elfhdr *elf = image;
  if (elf->magic != ELF_MAGIC)
    fail(L"kernel is not an ELF file", 0);

  // find the range of memory the kernel occupies.
  struct proghdr *ph = (struct proghdr *)((UINT8 *)image + elf->phoff);
  UINT64 lo = ~0ULL, hi = 0;
  for (int i = 0; i < elf->phnum; i++) {
    if (ph[i].type != ELF_PROG_LOAD)
      continue;
    if (ph[i].paddr < lo)
      lo = ph[i].paddr;
    if (ph[i].paddr + ph[i].memsz > hi)
      hi = ph[i].paddr + ph[i].memsz;
  }
  lo &= ~(PGSIZE - 1);
  print(L"  kernel: ");
  printhex(lo);
  print(L" - ");
  printhex(hi);
  print(L"\r\n");

  // [platform: real PC] EfiLoaderCode, not EfiLoaderData: newer PC firmware can map
  // data pages no-execute, and the kernel runs on the firmware's
  // page tables until it makes its own.
  UINT64 addr = lo;
  s = BS->AllocatePages(AllocateAddress, EfiLoaderCode,
                        (hi - lo + PGSIZE - 1) / PGSIZE, &addr);
  if (EFI_ERROR(s)) {
    showmemmap(lo, hi);
    fail(L"memory for kernel is not free", s);
  }

  for (int i = 0; i < elf->phnum; i++) {
    if (ph[i].type != ELF_PROG_LOAD)
      continue;
    UINT8 *dst = (UINT8 *)ph[i].paddr;
    memcpy(dst, (UINT8 *)image + ph[i].off, ph[i].filesz);
    memset(dst + ph[i].filesz, 0, ph[i].memsz - ph[i].filesz);
  }
  return elf->entry;
}

EFI_STATUS EFIAPI
efi_main(EFI_HANDLE imagehandle, EFI_SYSTEM_TABLE *systab)
{
  EFI_STATUS s;

  ST = systab;
  BS = systab->BootServices;
  BS->SetWatchdogTimer(0, 0, 0, 0);

  print(L"xv6 loader\r\n");

  // the file system the loader itself came from.
  EFI_LOADED_IMAGE *li;
  EFI_SIMPLE_FILE_SYSTEM *fs;
  EFI_FILE *root;
  EFI_GUID liguid = LOADED_IMAGE_GUID;
  EFI_GUID fsguid = SIMPLE_FILE_SYSTEM_GUID;
  if (EFI_ERROR(s = BS->HandleProtocol(imagehandle, &liguid, (void **)&li)))
    fail(L"no loaded image", s);
  if (EFI_ERROR(s = BS->HandleProtocol(li->DeviceHandle, &fsguid,
                                       (void **)&fs)))
    fail(L"no file system", s);
  if (EFI_ERROR(s = fs->OpenVolume(fs, &root)))
    fail(L"cannot open volume", s);

  struct bootinfo *bi = (struct bootinfo *)lowpages(1, PHYSTOP);
  memset(bi, 0, sizeof(*bi));

  UINT64 entry = loadkernel(root);

  // the file system image, which the kernel uses as a RAM disk.
  UINT64 size;
  EFI_FILE *f = openfile(root, L"\\fs.img", &size);
  bi->fsimg = lowpages((size + PGSIZE - 1) / PGSIZE, PHYSTOP);
  bi->fsimg_size = size;
  readfile(f, (void *)bi->fsimg, size);
  f->Close(f);

  // a page below 640KB, where other CPUs start in 16-bit mode.
  // [platform: real PC] real PCs may have none free; the kernel
  // then uses one CPU. QEMU and VirtualBox always have one.
  UINT64 apboot = 0xA0000 - 1;
  if (!EFI_ERROR(BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, 1,
                                   &apboot)))
    bi->apboot = apboot;
  else
    print(L"  no free page below 640KB: other CPUs cannot start\r\n");

  // the ACPI tables describe the CPUs and interrupt controllers.
  EFI_GUID acpi20 = ACPI_20_TABLE_GUID, acpi10 = ACPI_10_TABLE_GUID;
  for (UINTN i = 0; i < ST->NumberOfTableEntries; i++) {
    EFI_CONFIGURATION_TABLE *t = &ST->ConfigurationTable[i];
    if (guideq(&t->VendorGuid, &acpi20)) {
      bi->rsdp = (UINT64)t->VendorTable;
      break;
    }
    if (guideq(&t->VendorGuid, &acpi10))
      bi->rsdp = (UINT64)t->VendorTable;
  }

  // the frame buffer, for a screen console on machines
  // without a serial port.
  EFI_GRAPHICS_OUTPUT *gop;
  EFI_GUID gopguid = GRAPHICS_OUTPUT_GUID;
  if (!EFI_ERROR(BS->LocateProtocol(&gopguid, 0, (void **)&gop))) {
    bi->fb_base = gop->Mode->FrameBufferBase;
    bi->fb_size = gop->Mode->FrameBufferSize;
    bi->fb_width = gop->Mode->Info->HorizontalResolution;
    bi->fb_height = gop->Mode->Info->VerticalResolution;
    bi->fb_stride = gop->Mode->Info->PixelsPerScanLine;
    print(L"  screen: ");
    printdec(bi->fb_width);
    print(L"x");
    printdec(bi->fb_height);
    print(L", frame buffer at ");
    printhex(bi->fb_base);
    // 0: RGB, 1: BGR, 2: bit mask, 3: no frame buffer (BLT only)
    print(L", pixel format ");
    printdec(gop->Mode->Info->PixelFormat);
    print(L"\r\n");
    // [platform: real PC] some graphics cards offer only BLT
    // (copy) operations and no frame buffer the kernel can write.
    if (gop->Mode->Info->PixelFormat > 2)
      bi->fb_base = 0;
  } else {
    print(L"  no frame buffer: the kernel can only use the serial port\r\n");
  }
  print(L"  ACPI: ");
  printhex(bi->rsdp);
  print(L"\r\n");

  // get the memory map, and leave boot services. the map must be
  // current, so allocate the buffer first; ExitBootServices fails
  // if the map changed since GetMemoryMap, so retry.
  UINTN mapbytes = 16 * PGSIZE;
  bi->memmap = lowpages(mapbytes / PGSIZE, PHYSTOP);
  print(L"starting kernel\r\n");

  // [platform: real PC] the kernel draws over the screen at once;
  // on a real PC, with no serial port, this is the only time to
  // read the lines above. (QEMU and VirtualBox also copy them to
  // the serial port.)
  BS->Stall(3 * 1000 * 1000);

  for (int tries = 0;; tries++) {
    UINTN mapsize = mapbytes, mapkey, descsize;
    UINT32 descversion;
    s = BS->GetMemoryMap(&mapsize, (EFI_MEMORY_DESCRIPTOR *)bi->memmap,
                         &mapkey, &descsize, &descversion);
    if (EFI_ERROR(s))
      fail(L"cannot get memory map", s);
    bi->memmap_size = mapsize;
    bi->memmap_descsize = descsize;
    s = BS->ExitBootServices(imagehandle, mapkey);
    if (!EFI_ERROR(s))
      break;
    if (tries > 10)
      fail(L"cannot exit boot services", s);
  }

  ((kernel_entry)entry)(bi);
  for (;;)
    ;
}
