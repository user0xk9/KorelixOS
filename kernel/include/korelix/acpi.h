#pragma once
#include <stdint.h>

#define MAX_IOAPICS   8
#define MAX_OVERRIDES 16

struct ioapic_info { uint8_t id; uint32_t addr; uint32_t gsi_base; };
struct irq_override { uint8_t source; uint32_t gsi; uint16_t flags; };

struct acpi_info {
    int cpu_count;
    int ioapic_count;
    struct ioapic_info ioapics[MAX_IOAPICS];
    int override_count;
    struct irq_override overrides[MAX_OVERRIDES];
};

extern struct acpi_info acpi;
void acpi_init(void);