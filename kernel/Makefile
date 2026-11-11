.RECIPEPREFIX = >
.SUFFIXES:

CC      = gcc
CFLAGS  = -std=gnu11 -O2 -g -Wall -Wextra -ffreestanding -fno-stack-protector \
          -fno-pie -fno-pic -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse \
          -mno-sse2 -mno-red-zone -mcmodel=kernel -Iinclude
LDFLAGS = -nostdlib -static -fno-pie -no-pie -Wl,--build-id=none \
          -Wl,-z,max-page-size=0x1000 -T linker.ld

SRCS = $(shell find . -name '*.c')
ASMS = $(shell find . -name '*.asm')
OBJS = $(SRCS:.c=.o) $(ASMS:.asm=.asm.o)

kernel.elf: $(OBJS) linker.ld
> $(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

%.o: %.c
> $(CC) $(CFLAGS) -c $< -o $@

%.asm.o: %.asm
> nasm -f elf64 -g -F dwarf $< -o $@

clean:
> rm -f $(OBJS) kernel.elf

.PHONY: clean