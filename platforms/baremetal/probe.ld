ENTRY(_image_start)
SECTIONS {
    . = 0x40000000;
    .text : { KEEP(*(.text.boot)) *(.text .text.*) }
    .rodata : ALIGN(16) { *(.rodata .rodata.*) }
    .data : ALIGN(16) { *(.data .data.*) }
    __file_end = .;
    __file_size = __file_end - _image_start;
    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }
    .stack (NOLOAD) : ALIGN(16) {
        . += 65536;
        __stack_top = .;
    }
    __image_size = ALIGN(4096);
    /DISCARD/ : { *(.comment .note* .eh_frame* .ARM.exidx* .ARM.extab*) }
}
ASSERT(__image_size - _image_start < 0x80000, "Probe exceeds its staging window")

