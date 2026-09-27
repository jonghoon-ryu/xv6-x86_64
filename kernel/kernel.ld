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
    _trampoline = .;
    *(trampsec)
    . = ALIGN(0x1000);
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
    PROVIDE(etext = .);
  }

  .rodata : {
    . = ALIGN(16);
    *(.rodata .rodata.*)
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
