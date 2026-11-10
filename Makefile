.RECIPEPREFIX = >

ISO = build/korelix.iso
OVMF = /usr/share/edk2/x64/OVMF_CODE.4m.fd

all: $(ISO)

kernel:
> $(MAKE) -C kernel

$(ISO): kernel boot/limine/limine.conf
> rm -rf build/iso
> mkdir -p build/iso/boot/limine build/iso/EFI/BOOT
> cp kernel/kernel.elf build/iso/boot/kernel
> cp boot/limine/limine.conf build/iso/boot/limine/
> cp /usr/share/limine/limine-uefi-cd.bin build/iso/boot/limine/
> cp /usr/share/limine/BOOTX64.EFI build/iso/EFI/BOOT/
> xorriso -as mkisofs -R -r -J --efi-boot boot/limine/limine-uefi-cd.bin \
>   -efi-boot-part --efi-boot-image --protective-msdos-label build/iso -o $@

run: $(ISO)
> qemu-system-x86_64 -M q35 -m 512M \
>   -drive if=pflash,format=raw,readonly=on,file=$(OVMF) \
>   -cdrom $(ISO) -serial stdio

clean:
> $(MAKE) -C kernel clean
> rm -rf build

.PHONY: all kernel run clean