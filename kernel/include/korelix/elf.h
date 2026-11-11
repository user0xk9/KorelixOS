#pragma once
#include <stdint.h>
#include <stddef.h>

int elf_load(uint64_t *pml4, const void *image, size_t size, uint64_t *entry_out);