#!/usr/bin/env bash
# Общие функции для run.sh и debug.sh (подключается через source).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ISO="$ROOT/build/korelix.iso"

# Путь к прошивке UEFI: переменная OVMF или первый найденный стандартный путь.
find_ovmf() {
    if [[ -n "${OVMF:-}" ]]; then echo "$OVMF"; return; fi
    local c
    for c in \
        /usr/share/edk2/x64/OVMF_CODE.4m.fd \
        /usr/share/edk2/x64/OVMF_CODE.fd \
        /usr/share/edk2/ovmf/OVMF_CODE.fd \
        /usr/share/OVMF/OVMF_CODE_4M.fd \
        /usr/share/OVMF/OVMF_CODE.fd \
        /usr/share/ovmf/OVMF.fd \
        /usr/share/qemu/OVMF.fd; do
        if [[ -f "$c" ]]; then echo "$c"; return; fi
    done
    echo "OVMF не найден: установите edk2-ovmf (Arch) / ovmf (Debian, Ubuntu)" >&2
    echo "или задайте путь: OVMF=/путь/к/OVMF_CODE.fd" >&2
    return 1
}

# Собрать ISO (пропускается при NO_BUILD=1)
build_iso() {
    if [[ "${NO_BUILD:-0}" != 1 ]]; then
        make -C "$ROOT" build/korelix.iso
    fi
    [[ -f "$ISO" ]] || { echo "нет $ISO" >&2; return 1; }
}

# Базовые параметры QEMU. -no-reboot -no-shutdown: при тройной ошибке окно не
# закрывается и не перезагружается в цикле, состояние можно разглядеть.
qemu_base_args() {
    local ovmf
    ovmf="$(find_ovmf)"
    QEMU_ARGS=(
        -M q35 -cpu max -m "${MEM:-512M}"
        -drive "if=pflash,format=raw,readonly=on,file=$ovmf"
        -cdrom "$ISO"
        -serial stdio
        -no-reboot -no-shutdown
    )
}
