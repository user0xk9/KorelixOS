# План

## Сделано

- Загрузка через Limine (UEFI), higher-half ядро
- PMM, VMM (W^X), куча
- GDT/IDT, ACPI, LAPIC, IOAPIC, таймер, клавиатура PS/2
- Планировщик с вытеснением, FPU/SSE при переключении
- Ring 3, `syscall`/`sysret`, ELF, SMEP/SMAP/UMIP
- initramfs + VFS в памяти: запись, каталоги, `..`, cwd, `rename`, `unlink` открытых файлов, `lseek`, `stat`
- Оболочка с `cd`, `>`, `>>`, `<`, кавычками; команды `ls cat cp mv rm mkdir rmdir touch stat pwd tree wc df echo`
- `spawn` с наследованием cwd и подстановкой stdin/stdout
- Вывод на экран (framebuffer, UTF-8, кириллица)
- Сборка с `-Werror`, скрипты запуска и отладки, хостовые тесты под ASan/UBSan

## Дальше (в порядке приоритета)

1. **Дескрипторы:** fd 0/1/2 как обычные `file`, `dup2`, pipe — тогда в оболочке заработает `|`
2. **Права:** проверка родителя в `wait`, uid/capabilities и права на файлы, затем ASLR (стек и ELF)
3. **Диск:** сканирование PCI, AHCI или virtio-blk, блочный слой и ФС на диске (тогда файлы переживут перезагрузку)
4. **ФС:** время изменения файлов, `cp -r`, `ln`, права, `head`/`tail`/`grep`, история команд и стрелки в оболочке
5. **Экран:** write-combining для framebuffer (PAT), ANSI-последовательности (цвет, позиция курсора)
6. **Процессы:** `fork`/`exec`, `mmap`/`brk`, динамическая память процессов
7. **Позже:** SMP, своя libc побольше, графическая оболочка

## Убранные заготовки

Пустые файлы `security/caps.c`, `security/aslr.c`, `drivers/pci/pci.c`, `lib/printf.c`,
`arch/x86_64/boot/entry.asm` и `include/korelix/types.h` удалены; соответствующие
пункты выше создадут их заново, когда начнётся работа.
