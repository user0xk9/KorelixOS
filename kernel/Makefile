.RECIPEPREFIX = >
.SUFFIXES:

CC      = gcc

# Предупреждения = ошибки. Если новый компилятор ругается на то, что вы не трогали:
#   make WERROR= ...
WERROR ?= -Werror

# Отладочная сборка с UBSan без рантайма: проверка срабатывает инструкцией ud2,
# ядро получает #UD и печатает rip в панике (адрес -> строка: addr2line -e kernel.elf <rip>).
#   make UBSAN=1
ifdef UBSAN
SANFLAGS = -fsanitize=undefined -fsanitize-undefined-trap-on-error
endif

CFLAGS  = -std=gnu11 -O2 -g -Wall -Wextra $(WERROR) $(SANFLAGS) -MMD -MP \
          -ffreestanding -fno-stack-protector \
          -fno-pie -fno-pic -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse \
          -mno-sse2 -mno-red-zone -mcmodel=kernel -Iinclude
LDFLAGS = -nostdlib -static -fno-pie -no-pie -Wl,--build-id=none \
          -Wl,-z,max-page-size=0x1000 -T linker.ld

SRCS = $(shell find . -name '*.c')
ASMS = $(shell find . -name '*.asm')
OBJS = $(SRCS:.c=.o) $(ASMS:.asm=.asm.o)
DEPS = $(SRCS:.c=.d)

kernel.elf: $(OBJS) linker.ld
> $(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

%.o: %.c
> $(CC) $(CFLAGS) -c $< -o $@

%.asm.o: %.asm
> nasm -f elf64 -g -F dwarf $< -o $@

# Зависимости от заголовков (генерирует -MMD): правка .h пересобирает нужные .c
-include $(DEPS)

clean:
> rm -f $(OBJS) $(DEPS) kernel.elf

.PHONY: clean
