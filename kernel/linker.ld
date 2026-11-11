OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(kmain)

PHDRS {
    text   PT_LOAD;
    rodata PT_LOAD;
    data   PT_LOAD;
}

SECTIONS {
    . = 0xffffffff80000000;

    __text_start = .;
    .text : { *(.text .text.*) } :text
    __text_end = .;

    . = ALIGN(4K);
    __rodata_start = .;
    .rodata : { *(.rodata .rodata.*) } :rodata
    __rodata_end = .;

    . = ALIGN(4K);
    __data_start = .;
    .data : {
        *(.data .data.*)
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data
    __data_end = .;

    /DISCARD/ : { *(.eh_frame*) *(.note .note.*) }
}