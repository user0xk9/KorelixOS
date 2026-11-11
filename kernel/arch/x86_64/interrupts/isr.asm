bits 64
extern isr_handler
global isr_stub_table

section .text

%assign i 0
%rep 256
isr%+i:
%if i = 8 || (i >= 10 && i <= 14) || i = 17 || i = 21 || i = 29 || i = 30
    ; CPU сам положил код ошибки
%else
    push 0           ; фиктивный код ошибки
%endif
    push i
    jmp isr_common
%assign i i+1
%endrep

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, rsp
    cld
    call isr_handler
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16      ; убрать vector + error
    iretq

section .rodata
isr_stub_table:
%assign i 0
%rep 256
    dq isr%+i
%assign i i+1
%endrep