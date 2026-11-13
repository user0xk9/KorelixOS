bits 64
global syscall_entry
global enter_usermode
extern syscall_dispatch
extern syscall_kernel_rsp

section .bss
user_rsp_scratch: resq 1

section .text

; Вход по инструкции syscall: rcx = пользовательский rip, r11 = пользовательские rflags,
; rsp всё ещё ПОЛЬЗОВАТЕЛЬСКИЙ. Прерывания выключены маской FMASK.
; Соглашение: rax = номер, аргументы rdi rsi rdx r10 r8 r9, результат в rax;
; все регистры, кроме rax, rcx, r11, сохраняются.
syscall_entry:
    mov [rel user_rsp_scratch], rsp
    mov rsp, [rel syscall_kernel_rsp]

    push qword [rel user_rsp_scratch]
    push r11
    push rcx
    push rax
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    cld
    call syscall_dispatch

    cli                      ; ниже стек станет пользовательским: прерывание тут недопустимо
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi
    pop rax
    pop rcx
    pop r11
    pop rsp
    o64 sysret

; void enter_usermode(uint64_t rip, uint64_t rsp, uint64_t arg1, uint64_t arg2)
; rdi = rip, rsi = rsp, rdx = arg1 (-> rdi), rcx = arg2 (-> rsi).
; Строит кадр для iretq: SS=0x1B, RSP, RFLAGS (IF=1), CS=0x23, RIP
enter_usermode:
    push 0x1B
    push rsi
    push 0x202
    push 0x23
    push rdi

    mov rdi, rdx             ; argc
    mov rsi, rcx             ; argv

    xor eax, eax             ; остальное не отдаём пользователю
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    iretq