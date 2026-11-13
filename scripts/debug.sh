#!/usr/bin/env bash
# QEMU с отладчиком: ждёт gdb на localhost:1234, пишет прерывания и сбросы CPU в
# build/qemu.log (по нему видно, что случилось перед тройной ошибкой).
#
#   терминал 1:  scripts/debug.sh
#   терминал 2:  gdb -q -x scripts/gdbinit      (из корня проекта)
#
# Переменные окружения: OVMF=путь, MEM=1G, NO_BUILD=1, UBSAN=1
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

build_iso
qemu_base_args
mkdir -p "$ROOT/build"
echo "QEMU ждёт gdb. Во втором терминале:  gdb -q -x scripts/gdbinit" >&2
exec qemu-system-x86_64 "${QEMU_ARGS[@]}" -s -S -d int,cpu_reset -D "$ROOT/build/qemu.log" "$@"
