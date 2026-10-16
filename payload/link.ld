/* link.ld — раскладка payload blob: [0x40 заголовок — нули][код].
 * ENTRY_OFF = 0x40 зашит и в inject.h (kPayloadHdr), и в asm inj_entry. */
ENTRY(inj_entry)
SECTIONS
{
  . = 0x40;
  .text : {
    KEEP(*(.text.inj_entry))
    *(.text .text.*)
  }
  .rodata : { *(.rodata .rodata.*) }
  .data   : { *(.data .data.*) }
  .bss    : { *(.bss .bss.*) *(COMMON) }
  /DISCARD/ : { *(.eh_frame .eh_frame_hdr .note* .comment .ARM.attributes) }
}
