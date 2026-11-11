#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include <korelix/acpi.h>
#include <korelix/mm.h>
#include <korelix/log.h>
#include <korelix/string.h>

__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0
};

struct acpi_info acpi;

struct rsdp {
    char     sig[8];
    uint8_t  checksum;
    char     oem[6];
    uint8_t  revision;
    uint32_t rsdt_addr;
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t  ext_checksum;
    uint8_t  reserved[3];
} __attribute__((packed));

struct sdt_header {
    char     sig[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

/* Таблицы ACPI могут лежать в "reserved" памяти, которую мы не отображали в HHDM.
 * Досоздаём нужные 2-МБ страницы (те же флаги, что и в vmm_init, повтор безопасен). */
static const void *acpi_map(uint64_t phys, uint64_t len) {
    uint64_t start = ALIGN_DOWN(phys, 0x200000ULL);
    uint64_t end   = ALIGN_UP(phys + len, 0x200000ULL);
    for (uint64_t p = start; p < end; p += 0x200000ULL)
        vmm_map_2m(kernel_pml4, hhdm_base + p, p, PTE_WRITE | PTE_NX);
    return phys_to_virt(phys);
}

static const struct sdt_header *map_table(uint64_t phys) {
    const struct sdt_header *h = acpi_map(phys, sizeof(struct sdt_header));
    acpi_map(phys, h->length);
    return h;
}

void acpi_init(void) {
    if (!rsdp_request.response)
        panic("Bootloader gave no ACPI RSDP");

    uint64_t a = (uint64_t)rsdp_request.response->address;
    if (a >= hhdm_base) a -= hhdm_base;      /* в разных ревизиях протокола адрес то virt, то phys */

    const struct rsdp *rsdp = acpi_map(a, sizeof(struct rsdp));
    if (memcmp(rsdp->sig, "RSD PTR ", 8))
        panic("Bad RSDP signature");

    int use_xsdt = rsdp->revision >= 2 && rsdp->xsdt_addr;
    const struct sdt_header *root = map_table(use_xsdt ? rsdp->xsdt_addr : rsdp->rsdt_addr);

    uint32_t entry_size = use_xsdt ? 8 : 4;
    uint32_t entries = (root->length - sizeof(*root)) / entry_size;
    const uint8_t *tab = (const uint8_t *)root + sizeof(*root);

    const struct sdt_header *madt = NULL;
    for (uint32_t i = 0; i < entries && !madt; i++) {
        uint64_t t = 0;
        memcpy(&t, tab + i * entry_size, entry_size);   /* little-endian: хватает копирования */
        const struct sdt_header *h = map_table(t);
        if (!memcmp(h->sig, "APIC", 4)) madt = h;
    }
    if (!madt) panic("No MADT (APIC) table in ACPI");

    const uint8_t *e   = (const uint8_t *)madt + 44;    /* записи идут после 44-байтового заголовка */
    const uint8_t *end = (const uint8_t *)madt + madt->length;
    while (e + 2 <= end) {
        uint8_t type = e[0], len = e[1];
        if (len < 2) break;

        if (type == 0 && len >= 8) {                     /* Processor Local APIC */
            if (e[4] & 3) acpi.cpu_count++;              /* enabled или online-capable */
        } else if (type == 1 && len >= 12) {             /* IOAPIC */
            if (acpi.ioapic_count < MAX_IOAPICS) {
                struct ioapic_info *io = &acpi.ioapics[acpi.ioapic_count++];
                io->id = e[2];
                memcpy(&io->addr, e + 4, 4);
                memcpy(&io->gsi_base, e + 8, 4);
            }
        } else if (type == 2 && len >= 10) {             /* Interrupt Source Override */
            if (acpi.override_count < MAX_OVERRIDES) {
                struct irq_override *ov = &acpi.overrides[acpi.override_count++];
                ov->source = e[3];
                memcpy(&ov->gsi, e + 4, 4);
                memcpy(&ov->flags, e + 8, 2);
            }
        }
        e += len;
    }

    kprintf("ACPI: %d CPU(s), %d IOAPIC(s), %d IRQ override(s)\n",
            acpi.cpu_count, acpi.ioapic_count, acpi.override_count);
}