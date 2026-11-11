ENTRY(_start)

PHDRS {
    text   PT_LOAD FLAGS(5);    /* R+X */
    rodata PT_LOAD FLAGS(4);    /* R   */
    data   PT_LOAD FLAGS(6);    /* R+W */
}

SECTIONS {
    . = 0x400000;

    .text : { *(.text .text.*) } :text

    . = ALIGN(4K);
    .rodata : { *(.rodata .rodata.*) } :rodata

    . = ALIGN(4K);
    /* BYTE(0): секция не пустая, даже если у программы нет глобальных переменных */
    .data : { BYTE(0) *(.data .data.*) } :data
    .bss : { *(.bss .bss.*) *(COMMON) } :data

    /DISCARD/ : { *(.eh_frame*) *(.note .note.*) *(.comment) }
}