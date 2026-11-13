#!/usr/bin/env bash
# Сборка и запуск KorelixOS в QEMU (UEFI + Limine).
#
#   scripts/run.sh            окно QEMU, serial-лог в этом терминале
#   scripts/run.sh --nox      без окна: весь ввод и вывод идут через терминал
#   scripts/run.sh -- ARGS    всё после "--" передаётся в QEMU как есть
#
# Переменные окружения: OVMF=путь, MEM=1G, NO_BUILD=1, UBSAN=1 (отладочная сборка)
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

extra=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --nox)     extra+=(-display none) ;;
        -h|--help) sed -n '2,9p' "${BASH_SOURCE[0]}"; exit 0 ;;
        --)        shift; extra+=("$@"); break ;;
        *)         echo "неизвестный параметр: $1 (см. --help)" >&2; exit 2 ;;
    esac
    shift
done

build_iso
qemu_base_args
exec qemu-system-x86_64 "${QEMU_ARGS[@]}" "${extra[@]}"
