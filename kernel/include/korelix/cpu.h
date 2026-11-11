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
void irq_register(uint8_t vector, void (*handler)(struct regs *));
extern int smap_enabled;
void cpu_enable_protections(void);
void tss_set_rsp0(uint64_t rsp0);
extern uint8_t fpu_initial[512];
void fpu_init(void);

static inline void fpu_save(void *area) {
    __asm__ volatile("fxsave64 (%0)" : : "r"(area) : "memory");
}
static inline void fpu_restore(const void *area) {
    __asm__ volatile("fxrstor64 (%0)" : : "r"(area) : "memory");
}