#pragma once
#include <stdint.h>

/* Порядок полей = порядок на стеке в isr_common */
struct regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error;
    uint64_t rip, cs, rflags, rsp, ss;
};

void gdt_init(void);
void idt_init(void);