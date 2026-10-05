OUTPUT_ARCH( "i386:x86-64" )
ENTRY( _entry )

SECTIONS
{
  /*
   * the UEFI loader copies the kernel to KERNBASE (0x100000)
   * and jumps to _entry.
   */
  . = 0x100000;

  .text : {
    kernel/entry.o(.text)
    *(.text .text.*)
    . = ALIGN(0x1000);
    PROVIDE(etext = .);
  }

  .rodata : {
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  /* constructors of C++ global objects; start() runs them. */
  .init_array : {
    . = ALIGN(8);
    PROVIDE(__init_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    PROVIDE(__init_array_end = .);
  }

  .data : {
    . = ALIGN(16);
    *(.data .data.*)
  }

  .bss : {
    . = ALIGN(16);
    *(.bss .bss.*)
    *(COMMON)
  }

  . = ALIGN(0x1000);
  PROVIDE(end = .);

  /DISCARD/ : {
    *(.eh_frame .note.GNU-stack .note.gnu.property .comment)
  }
}
