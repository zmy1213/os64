ENTRY(kernel_entry)

PHDRS {
  code PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
  zero PT_LOAD FLAGS(6);
}

SECTIONS {
  . = 0x00010000;                     /* 这一轮先把整个内核固定链接到 64 KiB。 */

  .text : {
    KEEP(*(.text.entry))              /* 先保证 kernel_entry 放在最前面。 */
    *(.text*)
  } :code

  .rodata : {
    *(.rodata*)
  } :code

  .data : {
    *(.data*)
  } :data

  __kernel_file_end = .;
  ASSERT(__kernel_file_end <= 0x80000, "Kernel file overlaps BIOS boot volume")

  /* BSS has no file bytes. Keep it above the BIOS loader's low-memory volume,
     explicitly zero it at entry, and reserve the full window in the PMM. */
  . = 0x00100000;

  .bss (NOLOAD) : {
    __kernel_bss_start = .;
    *(.bss*)
    *(COMMON)
    __kernel_bss_end = .;
  } :zero
  ASSERT(__kernel_bss_end <= 0x160000, "Kernel BSS exceeds reserved data window")
}
