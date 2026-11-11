#include <stdint.h>
#include <cpuid.h>
#include <korelix/cpu.h>
#include <korelix/log.h>

int smap_enabled;

void cpu_enable_protections(void) {
    uint32_t a = 0, b = 0, c = 0, d = 0;
    uint64_t cr4;
    int smep = 0, smap = 0, umip = 0;

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

    if (__get_cpuid_count(7, 0, &a, &b, &c, &d)) {
        smep = (b >> 7) & 1;
        smap = (b >> 20) & 1;
        umip = (c >> 2) & 1;
    }

    if (smep) cr4 |= 1ULL << 20;     /* ядро не исполняет код из пользовательских страниц */
    if (smap) cr4 |= 1ULL << 21;     /* ядро не читает/пишет пользовательские страницы без stac */
    if (umip) cr4 |= 1ULL << 11;     /* ring 3 не может читать sgdt/sidt и т.п. */
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));

    smap_enabled = smap;
    kprintf("CPU protections: SMEP %s, SMAP %s, UMIP %s\n",
            smep ? "on" : "off", smap ? "on" : "off", umip ? "on" : "off");
}
/* Эталонное «чистое» состояние FPU/SSE: его получает каждая новая задача */
uint8_t fpu_initial[512] __attribute__((aligned(16)));

void fpu_init(void) {
    uint64_t cr0, cr4;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2);                 /* EM = 0: FPU есть */
    cr0 |=  (1ULL << 1);                 /* MP = 1 */
    cr0 &= ~(1ULL << 3);                 /* TS = 0: без «ленивого» переключения */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9) | (1ULL << 10);   /* OSFXSR, OSXMMEXCPT: разрешаем SSE */
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));

    __asm__ volatile("fninit");
    uint32_t mxcsr = 0x1F80;             /* значение по умолчанию после сброса */
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
    fpu_save(fpu_initial);
}