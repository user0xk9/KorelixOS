.RECIPEPREFIX = >

ISO = build/korelix.iso
INITRD = build/initrd.tar
OVMF = /usr/share/edk2/x64/OVMF_CODE.4m.fd

all: $(ISO)

kernel:
> $(MAKE) -C kernel

userland:
> $(MAKE) -C userland

# Корневая ФС: статические файлы из initrd/ + собранные программы в /bin
$(INITRD): userland $(shell find initrd -type f)
> rm -rf build/rootfs
> mkdir -p build/rootfs/bin build/rootfs/etc
> cp -r initrd/. build/rootfs/
> cp userland/init/init.elf build/rootfs/bin/init
> cp userland/fpu/fpu.elf build/rootfs/bin/fpu
> tar --format=ustar --owner=0 --group=0 -cf $@ -C build/rootfs .

$(ISO): kernel $(INITRD) boot/limine/limine.conf
> rm -rf build/iso
> mkdir -p build/iso/boot/limine build/iso/EFI/BOOT
> cp kernel/kernel.elf build/iso/boot/kernel
> cp $(INITRD) build/iso/boot/initrd.tar
> cp boot/limine/limine.conf build/iso/boot/limine/
> cp /usr/share/limine/limine-uefi-cd.bin build/iso/boot/limine/
> cp /usr/share/limine/BOOTX64.EFI build/iso/EFI/BOOT/
> xorriso -as mkisofs -R -r -J --efi-boot boot/limine/limine-uefi-cd.bin \
>   -efi-boot-part --efi-boot-image --protective-msdos-label build/iso -o $@

run: $(ISO)
> qemu-system-x86_64 -M q35 -cpu max -m 512M \
>   -drive if=pflash,format=raw,readonly=on,file=$(OVMF) \
>   -cdrom $(ISO) -serial stdio

clean:
> $(MAKE) -C kernel clean
> $(MAKE) -C userland clean
> rm -rf build

.PHONY: all kernel userland run clean