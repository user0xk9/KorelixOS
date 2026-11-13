.RECIPEPREFIX = >

ISO = build/korelix.iso
INITRD = build/initrd.tar
LIMINE_DIR ?= /usr/share/limine

all: $(ISO)

kernel:
> $(MAKE) -C kernel

userland:
> $(MAKE) -C userland

# Корневая ФС: статические файлы из initrd/ + все собранные программы в /bin
$(INITRD): userland $(shell find initrd -type f)
> rm -rf build/rootfs
> mkdir -p build/rootfs/bin build/rootfs/etc
> cp -r initrd/. build/rootfs/
> for f in userland/*/*.elf; do cp $$f build/rootfs/bin/$$(basename $$f .elf); done
> tar --format=ustar --owner=0 --group=0 -cf $@ -C build/rootfs .

$(ISO): kernel $(INITRD) boot/limine/limine.conf
> rm -rf build/iso
> mkdir -p build/iso/boot/limine build/iso/EFI/BOOT
> cp kernel/kernel.elf build/iso/boot/kernel
> cp $(INITRD) build/iso/boot/initrd.tar
> cp boot/limine/limine.conf build/iso/boot/limine/
> cp $(LIMINE_DIR)/limine-uefi-cd.bin build/iso/boot/limine/
> cp $(LIMINE_DIR)/BOOTX64.EFI build/iso/EFI/BOOT/
> xorriso -as mkisofs -R -r -J --efi-boot boot/limine/limine-uefi-cd.bin \
>   -efi-boot-part --efi-boot-image --protective-msdos-label build/iso -o $@

# Запуск и отладка в QEMU: подробности в scripts/run.sh и scripts/debug.sh
run:
> scripts/run.sh

debug:
> scripts/debug.sh

# Хостовые тесты (ASan + UBSan): string, initramfs/VFS, консоль на framebuffer
test:
> $(MAKE) -C tests

clean:
> $(MAKE) -C kernel clean
> $(MAKE) -C userland clean
> $(MAKE) -C tests clean
> rm -rf build

.PHONY: all kernel userland run debug test clean
