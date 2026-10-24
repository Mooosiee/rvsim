OUTPUT_ARCH(riscv)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
  . = 0x1000;
  .text : { KEEP(*(.text.start)) *(.text .text.*) *(.rodata .rodata.*) } :text
  ASSERT(. <= 0x2000, "demo code exceeds reserved text space")
  . = 0x2000;
  .data : { *(.data .data.*) *(.sdata .sdata.*) } :data
  .bss (NOLOAD) : { __bss_start = .; *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) __bss_end = .; } :data
  __stack_top = 0x10000;
  ASSERT(. <= 0xf000, "demo data collides with stack")
  /DISCARD/ : { *(.comment) *(.riscv.attributes) *(.eh_frame*) }
}
