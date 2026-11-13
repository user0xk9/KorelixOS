.RECIPEPREFIX = >
.SUFFIXES:

# Хостовые тесты ядерного кода под ASan + UBSan (любая ошибка = падение теста)
CC     = gcc
SAN    = -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
CFLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Werror $(SAN) -Istubs -I../kernel/include
export ASAN_OPTIONS = detect_leaks=0

K = ../kernel
BUILD = build

TESTS = test_string test_initramfs test_vfs test_userland test_fbcon

all: $(TESTS:%=$(BUILD)/%)
> @for t in $(TESTS); do $(BUILD)/$$t || exit 1; done

$(BUILD):
> mkdir -p $@

$(BUILD)/test_string: test_string.c $(K)/lib/string.c check.h | $(BUILD)
> $(CC) $(CFLAGS) -fno-builtin -Dmemcpy=k_memcpy -Dmemset=k_memset -Dmemmove=k_memmove -Dmemcmp=k_memcmp \
>   test_string.c $(K)/lib/string.c -o $@

$(BUILD)/test_initramfs: test_initramfs.c $(K)/fs/vfs.c $(K)/fs/initramfs.c check.h kstubs.h | $(BUILD)
> $(CC) $(CFLAGS) test_initramfs.c $(K)/fs/vfs.c $(K)/fs/initramfs.c -o $@

$(BUILD)/test_vfs: test_vfs.c $(K)/fs/vfs.c check.h kstubs.h | $(BUILD)
> $(CC) $(CFLAGS) test_vfs.c $(K)/fs/vfs.c -o $@

# Настоящие userland-программы, собранные для хоста (ulib.h вызывает host_syscall)
UPROGS = sh ls cat echo mkdir rmdir rm touch cp mv stat pwd tree wc df
USRC   = $(UPROGS:%=../userland/%/%.c)
UCFLAGS = $(CFLAGS) -DULIB_HOST -I../userland/libc

$(BUILD)/test_userland: test_userland.c $(K)/fs/vfs.c $(wildcard ../userland/*/*.c) ../userland/libc/ulib.h check.h kstubs.h | $(BUILD)
> for p in $(UPROGS); do \
>   $(CC) $(UCFLAGS) -D_start=prog_$$p -c ../userland/$$p/$$p.c -o $(BUILD)/u_$$p.o || exit 1; \
> done
> $(CC) $(CFLAGS) test_userland.c $(BUILD)/u_*.o $(K)/fs/vfs.c -o $@

$(BUILD)/test_fbcon: test_fbcon.c $(K)/drivers/video/fb.c $(K)/drivers/video/font8x16.h check.h | $(BUILD)
> $(CC) $(CFLAGS) test_fbcon.c $(K)/drivers/video/fb.c -o $@

clean:
> rm -rf $(BUILD)

.PHONY: all clean
