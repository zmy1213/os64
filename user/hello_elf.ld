ENTRY(_start)

PHDRS
{
  /* 第一个 PT_LOAD 放代码。FILEHDR + PHDRS 的意思是：
     ELF header 和 program header 也一起落进这个可加载段里。 */
  text PT_LOAD FLAGS(5) FILEHDR PHDRS;
  /* 第二个 PT_LOAD 放 .data/.bss，逼着内核真正支持多段装载。 */
  data PT_LOAD FLAGS(6);
}

SECTIONS
{
  /* 代码仍然放在教学用户窗口 0x400000 附近。 */
  . = 0x400000 + SIZEOF_HEADERS;

  .text : {
    *(.text*)
  } :text

  /* 数据段故意拉到下一页，这样装载器必须真的再映射一张页。 */
  . = 0x401000;

  .data : {
    *(.data*)
    *(.rodata*)
  } :data

  .bss : {
    *(.bss*)
  } :data
}
