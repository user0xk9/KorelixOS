.RECIPEPREFIX = >
.SUFFIXES:

CC      = gcc
WERROR ?= -Werror
CFLAGS  = -std=gnu11 -O2 -Wall -Wextra $(WERROR) -ffreestanding -fno-stack-protector \
          -fno-pie -fno-pic -m64 -Ilibc
LDFLAGS = -nostdlib -static -no-pie -Wl,--build-id=none \
          -Wl,-z,max-page-size=0x1000 -T user.ld

PROGS = init sh ls cat echo fpu mkdir rmdir rm touch cp mv stat pwd tree wc df
ELFS  = $(foreach p,$(PROGS),$(p)/$(p).elf)

all: $(ELFS)

%.elf: %.c libc/ulib.h user.ld
> $(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

clean:
> rm -f $(ELFS)

.PHONY: all clean