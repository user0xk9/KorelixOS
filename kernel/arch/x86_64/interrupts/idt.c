#include <stdint.h>
#include <korelix/cpu.h>
#include <korelix/apic.h>
#include <korelix/log.h>

struct idt_entry {
    uint16_t off_lo;
    uint16_t sel;
    uint8_t  ist;
    uint8_t  attr;
    uint16_t off_mid;
    uint32_t off_hi;
    uint32_t zero;
} __attribute__((packed));

struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static void (*irq_handlers[256])(struct regs *);
extern uint64_t isr_stub_table[256];

static const char *names[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint", "Overflow",
    "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS",
    "Segment Not Present", "Stack-Segment Fault", "General Protection Fault",
    "Page Fault", "Reserved", "x87 FPU Error", "Alignment Check",
    "Machine Check", "SIMD FP Exception", "Virtualization Exception",
    "Control Protection", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Hypervisor Injection", "VMM Communication",
    "Security Exception", "Reserved"
};

static void set_gate(int n, uint64_t handler, uint8_t ist) {
    idt[n] = (struct idt_entry){
        .off_lo = handler & 0xFFFF,
        .sel = 0x08,
        .ist = ist,
        .attr = 0x8E,                    /* present, ring 0, interrupt gate */
        .off_mid = (handler >> 16) & 0xFFFF,
        .off_hi = handler >> 32,
        .zero = 0
    };
}

void irq_register(uint8_t vector, void (*handler)(struct regs *)) {
    irq_handlers[vector] = handler;
}

void idt_init(void) {
    for (int i = 0; i < 256; i++)
        set_gate(i, isr_stub_table[i], i == 8 ? 1 : 0);   /* #DF на своём стеке */

    struct idtr r = { sizeof(idt) - 1, (uint64_t)&idt };
    __asm__ volatile("lidt %0" : : "m"(r));
}

void isr_handler(struct regs *r) {
    /* Аппаратные прерывания и всё, что выше 31 */
    if (r->vector >= 32) {
        if (r->vector == VEC_SPURIOUS) return;      /* ложное: EOI не нужен */

        if (r->vector == VEC_TIMER) lapic_eoi();    /* до возможного переключения задач */
        if (irq_handlers[r->vector])
            irq_handlers[r->vector](r);
        else
            kprintf("Unhandled interrupt %lu\n", r->vector);
        if (r->vector != VEC_TIMER) lapic_eoi();
        return;
    }

    /* Исключения процессора */
    log_enter_panic();      /* вывод без блокировок: вдруг упали внутри kprintf */

    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    kprintf("\n*** EXCEPTION %lu: %s ***\n", r->vector, names[r->vector]);
    kprintf("error=%lx rip=%lx cs=%lx rflags=%lx\n", r->error, r->rip, r->cs, r->rflags);
    kprintf("rax=%lx rbx=%lx rcx=%lx rdx=%lx\n", r->rax, r->rbx, r->rcx, r->rdx);
    kprintf("rsi=%lx rdi=%lx rbp=%lx rsp=%lx\n", r->rsi, r->rdi, r->rbp, r->rsp);
    if (r->vector == 14)
        kprintf("cr2=%lx (адрес, к которому обратились)\n", cr2);

    panic("Unhandled CPU exception");
}