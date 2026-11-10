bits 64
global gdt_load

section .text
gdt_load:
    lgdt [rdi]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax
    pop rax          ; адрес возврата
    push 0x08        ; новый CS
    push rax
    retfq            ; far return: перезагружает CS