bits 64
global switch_context

section .text

; void switch_context(uint64_t *old_rsp, uint64_t new_rsp)
; Сохраняет callee-saved регистры на стеке старой задачи, переключает стек и
; достаёт регистры новой. Флаги прерываний сохраняет вызывающий (schedule).
switch_context:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp
    mov rsp, rsi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret