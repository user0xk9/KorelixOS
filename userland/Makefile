.RECIPEPREFIX = >
.SUFFIXES:

CC      = gcc
CFLAGS  = -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-stack-protector \
          -fno-pie -fno-pic -m64 -Ilibc
LDFLAGS = -nostdlib -static -no-pie -Wl,--build-id=none \
          -Wl,-z,max-page-size=0x1000 -T user.ld

PROGS = init/init.elf fpu/fpu.elf

all: $(PROGS)

%.elf: %.c libc/ulib.h user.ld
> $(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

clean:
> rm -f $(PROGS)

.PHONY: all clean