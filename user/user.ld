OUTPUT_ARCH( "i386:x86-64" )
ENTRY( start )

SECTIONS
{
 . = 0x0;
 
  .text : {
    *(.text .text.*)
  }

  .rodata : {
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  .eh_frame : {
       *(.eh_frame)
       *(.eh_frame.*)
   }

  . = ALIGN(0x1000);
  .data : {
    . = ALIGN(16);
    *(.data .data.*)
  }

  .bss : {
    . = ALIGN(16);
    *(.bss .bss.*)
  }

  PROVIDE(end = .);
}
