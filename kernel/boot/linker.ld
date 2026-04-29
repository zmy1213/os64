ENTRY(kernel_entry)

SECTIONS {
  . = 0x00010000;                     /* 这一轮先把整个内核固定链接到 64 KiB。 */

  .text : {
    KEEP(*(.text.entry))              /* 先保证 kernel_entry 放在最前面。 */
    *(.text*)
  }

  .rodata : {
    *(.rodata*)
  }

  .data : {
    *(.data*)
  }

  .bss : {
    *(.bss*)
    *(COMMON)
  }
}
