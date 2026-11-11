#include <stdint.h>
#include <korelix/cpu.h>

struct gdt_entry {
    uint16_t limit_low, base_low;
    uint8_t base_mid, access, gran, base_high;
} __attribute__((packed));

struct tss_desc {
    uint16_t limit_low, base_low;
    uint8_t base_mid, access, gran, base_high;
    uint32_t base_upper, reserved;
} __attribute__((packed));

struct tss {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct {
    struct gdt_entry e[5];
    struct tss_desc tss;
} __attribute__((packed)) gdt;

static struct tss tss;
static uint8_t ist1_stack[16384] __attribute__((aligned(16)));

extern void gdt_load(struct gdtr *r);

static struct gdt_entry entry(uint8_t access, uint8_t gran) {
    return (struct gdt_entry){
        .limit_low = 0xFFFF, .base_low = 0, .base_mid = 0,
        .access = access, .gran = gran, .base_high = 0
    };
}

void gdt_init(void) {
    gdt.e[0] = (struct gdt_entry){0};
    gdt.e[1] = entry(0x9A, 0xAF);  /* 0x08 kernel code */
    gdt.e[2] = entry(0x92, 0xCF);  /* 0x10 kernel data */
    gdt.e[3] = entry(0xF2, 0xCF);  /* 0x18 user data   */
    gdt.e[4] = entry(0xFA, 0xAF);  /* 0x20 user code   */

    uint64_t base = (uint64_t)&tss;
    gdt.tss = (struct tss_desc){
        .limit_low = sizeof(tss) - 1,
        .base_low = base & 0xFFFF,
        .base_mid = (base >> 16) & 0xFF,
        .access = 0x89,
        .gran = 0,
        .base_high = (base >> 24) & 0xFF,
        .base_upper = base >> 32,
        .reserved = 0
    };

    tss.iomap_base = sizeof(tss);
    tss.ist[0] = (uint64_t)(ist1_stack + sizeof(ist1_stack));

    struct gdtr r = { sizeof(gdt) - 1, (uint64_t)&gdt };
    gdt_load(&r);
    __asm__ volatile("ltr %w0" : : "r"((uint16_t)0x28));
}
void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp[0] = rsp0;      /* стек, на который процессор перейдёт при прерывании из ring 3 */
}