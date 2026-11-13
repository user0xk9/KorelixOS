# KorelixOS

Учебная 64-битная операционная система для x86_64 с нуля: ядро на C и NASM, загрузка
через [Limine](https://github.com/limine-bootloader/limine) (UEFI), вытесняющая
многозадачность, процессы в ring 3, системные вызовы, ELF-программы, простая ФС и
оболочка. Вывод идёт и в serial, и на экран (framebuffer, шрифт с кириллицей).

## Что уже работает

- Загрузка: Limine (UEFI), higher-half ядро, W^X-таблицы страниц
- Память: PMM (битовая карта), VMM, куча ядра, адресные пространства процессов
- Прерывания: GDT/IDT, ACPI (MADT), LAPIC, IOAPIC, таймер LAPIC (100 Гц), клавиатура PS/2
- Планировщик: round-robin с вытеснением, спящие и ждущие задачи, сохранение FPU/SSE
- Userspace: `syscall`/`sysret`, ELF-загрузчик, SMEP/SMAP/UMIP, `spawn`/`wait`
- ФС: initramfs (tar) + записываемая VFS в памяти: файлы, каталоги, `..`, относительные пути, рабочий каталог
- Оболочка: `cd`, `>`, `>>`, `<`, кавычки; команды `ls cat cp mv rm mkdir rmdir touch stat pwd tree wc df echo`
- Консоль: вывод на экран (8x16, ASCII + русские/украинские буквы), serial, строковый ввод

Подробности: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), безопасность:
[docs/SECURITY.md](docs/SECURITY.md), планы: [docs/ROADMAP.md](docs/ROADMAP.md).

## Сборка и запуск

Пакеты (Arch): `base-devel nasm xorriso qemu-system-x86 edk2-ovmf limine`
(для отладки ещё `gdb`). В других дистрибутивах нужны те же компоненты (gcc, nasm, xorriso,
QEMU, OVMF); если пакета Limine нет, возьмите релиз с файлами `BOOTX64.EFI` и
`limine-uefi-cd.bin` и укажите каталог: `make LIMINE_DIR=/путь`.

```sh
make              # собрать build/korelix.iso
make run          # собрать и запустить в QEMU (окно + serial в терминале)
make debug        # QEMU ждёт gdb; во втором терминале: gdb -q -x scripts/gdbinit
make test         # хостовые тесты под ASan/UBSan, включая сценарии оболочки (QEMU не нужен)
make clean
```

Полезные параметры:

| Что | Как |
| --- | --- |
| Без окна, всё через терминал | `scripts/run.sh --nox` |
| Другой путь к OVMF | `OVMF=/путь/OVMF_CODE.fd make run` |
| Другая память | `MEM=1G make run` |
| Каталог Limine | `make LIMINE_DIR=/путь` |
| Отладочная сборка с UBSan | `make clean && make UBSAN=1 run` |
| Не ронять сборку из-за предупреждений нового компилятора | `make WERROR=` |

После `make UBSAN=1` нарушение даёт `#UD` и панику с `rip`; строку находит
`addr2line -e kernel/kernel.elf <rip>`.

## Работа в системе

После загрузки `init` показывает `/etc/motd` и запускает `sh`; в приглашении виден текущий
каталог (`korelix:/docs> `). Команда `help` перечисляет всё ниже.

| Команда | Что делает |
| --- | --- |
| `cd [dir]`, `pwd` | сменить каталог (без аргумента — корень) / показать текущий |
| `ls [-l] [path...]` | список по алфавиту; `-l` — тип и размер |
| `cat [file...]` | вывести файлы; без аргументов копирует ввод (Ctrl-D — конец) |
| `echo [-n] text` | вывести текст |
| `mkdir [-p] dir...`, `rmdir dir...` | создать каталог (с `-p` и родителей) / удалить пустой |
| `touch file...` | создать пустой файл |
| `cp src dst`, `mv src dst` | копировать файл / переименовать или перенести; если `dst` каталог, кладёт внутрь |
| `rm [-rf] path...` | удалить файл; `-r` — каталог целиком |
| `stat path...`, `tree [dir]`, `wc [file...]`, `df` | сведения о файле, дерево, счётчики, занятость ФС |

Оболочка понимает `команда > файл` (перезапись), `>> файл` (дозапись), `< файл`, кавычки
(`echo "два  слова"`) и относительные пути с `.` и `..`. Сообщения об ошибках идут в stderr и на
перенаправление не попадают.

```
korelix:/> mkdir -p docs/notes
korelix:/> echo "первая строка" > docs/notes/a.txt
korelix:/> cp docs/notes/a.txt docs/b.txt
korelix:/> cd docs
korelix:/docs> tree
```

Важно: файлы живут только в памяти и пропадают при перезагрузке (диска пока нет). Размер файла
ограничен 8 МБ. Содержимое файлов из initramfs при первой записи копируется в память.
Русский текст на экране отображается, но набрать его нельзя: клавиатура читается как US-раскладка.

## Структура

```
boot/limine/       конфиг загрузчика
kernel/            ядро (arch, drivers, mm, sched, syscall, fs, exec, lib)
  drivers/video/   framebuffer-консоль и шрифт (font8x16.h генерируется)
userland/          программы ring 3 и мини-библиотека ulib.h
initrd/            статические файлы корневой ФС
scripts/           run.sh, debug.sh, gdbinit
tests/             хостовые тесты (string, initramfs, VFS, оболочка и команды, консоль)
tools/             gen_font.py — генератор таблицы шрифта
docs/              архитектура, безопасность, план
```

## Шрифт

Глифы взяты из GNU Unifont (GPL-2.0-or-later с исключением для встраивания шрифтов;
условия проверяйте на unifoundry.com, если будете публиковать проект под другой лицензией).
Таблица генерируется командой `tools/gen_font.py > kernel/drivers/video/font8x16.h`
(нужен Pillow и файл `unifont.otf`); набор символов задаётся в начале скрипта.
