#include <stdint.h>
#include <cpuid.h>
#include <korelix/io.h>
#include <korelix/apic.h>
#include <korelix/acpi.h>
#include <korelix/mm.h>
#include <korelix/log.h>

#define MSR_APIC_BASE 0x1B

static volatile uint32_t *lapic;
static volatile uint32_t *ioapic_regs[MAX_IOAPICS];
static uint32_t ioapic_pins[MAX_IOAPICS];

/* ---------- Local APIC ---------- */

uint32_t lapic_read(uint32_t reg)              { return lapic[reg / 4]; }
void     lapic_write(uint32_t reg, uint32_t v) { lapic[reg / 4] = v; }
uint32_t lapic_id(void)                        { return lapic_read(LAPIC_ID) >> 24; }
void     lapic_eoi(void)                       { if (lapic) lapic_write(LAPIC_EOI, 0); }

void lapic_init(void) {
    uint32_t a, b, c, d;
    if (!__get_cpuid(1, &a, &b, &c, &d) || !(d & (1u << 9)))
        panic("CPU has no local APIC");

    uint64_t base = rdmsr(MSR_APIC_BASE);
    if (base & (1ULL << 10))
        panic("x2APIC mode is not supported yet");
    wrmsr(MSR_APIC_BASE, base | (1ULL << 11));          /* глобально включить */

    lapic = vmm_map_mmio(base & PTE_ADDR_MASK, 4096);   /* без кэша */

    lapic_write(LAPIC_TPR, 0);                          /* принимать все приоритеты */
    lapic_write(LAPIC_LINT0, 1u << 16);                 /* PIC-проброс не нужен */
    lapic_write(LAPIC_SVR, (1u << 8) | VEC_SPURIOUS);   /* включить APIC */

    kprintf("LAPIC: id=%u base=%lx\n", lapic_id(), base & PTE_ADDR_MASK);
}

/* ---------- IOAPIC ---------- */

static uint32_t ioapic_read(int n, uint8_t reg) {
    ioapic_regs[n][0] = reg;       /* IOREGSEL */
    return ioapic_regs[n][4];      /* IOWIN (смещение 0x10) */
}

static void ioapic_write(int n, uint8_t reg, uint32_t v) {
    ioapic_regs[n][0] = reg;
    ioapic_regs[n][4] = v;
}

void ioapic_init(void) {
    if (!acpi.ioapic_count) panic("No IOAPIC found");

    for (int n = 0; n < acpi.ioapic_count; n++) {
        ioapic_regs[n] = vmm_map_mmio(acpi.ioapics[n].addr, 4096);
        uint32_t pins = ((ioapic_read(n, 1) >> 16) & 0xFF) + 1;
        ioapic_pins[n] = pins;
        for (uint32_t i = 0; i < pins; i++)
            ioapic_write(n, 0x10 + 2 * i, 1u << 16);    /* всё замаскировано */
        kprintf("IOAPIC %d: id=%u gsi_base=%u pins=%u\n",
                n, acpi.ioapics[n].id, acpi.ioapics[n].gsi_base, pins);
    }
}

/* Направить ISA-прерывание (IRQ 0-15) на вектор текущего процессора */
void ioapic_route_isa_irq(uint8_t irq, uint8_t vector) {
    uint32_t gsi = irq, flags = 0;
    for (int i = 0; i < acpi.override_count; i++) {
        if (acpi.overrides[i].source == irq) {
            gsi = acpi.overrides[i].gsi;
            flags = acpi.overrides[i].flags;
            break;
        }
    }

    uint32_t low = vector;                    /* fixed delivery, physical dest, не замаскировано */
    if ((flags & 3) == 3)        low |= 1u << 13;   /* active low */
    if (((flags >> 2) & 3) == 3) low |= 1u << 15;   /* level-triggered */

    for (int n = 0; n < acpi.ioapic_count; n++) {
        uint32_t base = acpi.ioapics[n].gsi_base;
        if (gsi >= base && gsi < base + ioapic_pins[n]) {
            uint32_t pin = gsi - base;
            ioapic_write(n, 0x11 + 2 * pin, lapic_id() << 24);   /* куда: этот процессор */
            ioapic_write(n, 0x10 + 2 * pin, low);                /* и размаскируем */
            return;
        }
    }
    kprintf("IOAPIC: no IOAPIC handles GSI %u\n", gsi);
}