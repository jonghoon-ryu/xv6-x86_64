// what the UEFI loader (boot/loader.c) tells the kernel.
// shared by the loader and the kernel, which are built by
// different compilers, so it uses only 64-bit fields.

struct bootinfo {
  unsigned long long memmap;          // physical address of UEFI memory map
  unsigned long long memmap_size;     // bytes in the memory map
  unsigned long long memmap_descsize; // bytes per memory map entry
  unsigned long long fsimg;           // physical address of fs.img
  unsigned long long fsimg_size;      // bytes in fs.img
  unsigned long long rsdp;            // ACPI root pointer, or 0
  unsigned long long apboot;          // a free page below 1MB, for entryother.S
  unsigned long long fb_base;         // GOP frame buffer, or 0
  unsigned long long fb_size;
  unsigned long long fb_width;
  unsigned long long fb_height;
  unsigned long long fb_stride; // pixels per scan line
};

// one entry in the UEFI memory map (EFI_MEMORY_DESCRIPTOR).
// entries are memmap_descsize bytes apart, which may be larger.
struct efi_memdesc {
  unsigned int type;
  unsigned int pad;
  unsigned long long phys_start;
  unsigned long long virt_start;
  unsigned long long npages;
  unsigned long long attribute;
};

#define EFI_CONVENTIONAL_MEMORY 7
