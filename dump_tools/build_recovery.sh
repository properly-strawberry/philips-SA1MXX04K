#!/usr/bin/env bash
# Build Rockbox's RAM helper and inspection tools. No USB devices are opened.
set -euo pipefail

task_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$task_dir/rockbox"
log_file="$task_dir/build_recovery.log"
exec > >(tee "$log_file") 2>&1

for tool in gcc g++ make pkg-config arm-none-eabi-gcc arm-none-eabi-objcopy; do
    if ! command -v "$tool" >/dev/null; then
        printf 'Required tool is missing: %s\n' "$tool"
        exit 1
    fi
done
pkg-config --exists libusb-1.0
test -f "$source_dir/utils/hwstub/stub/stmp/Makefile"

printf 'Building source revision: '
git -C "$source_dir" rev-parse HEAD
arm-none-eabi-gcc --version | head -n 1

make -C "$source_dir/utils/imxtools/sbtools"
make -C "$source_dir/utils/hwstub/stub/stmp" PREFIX=arm-none-eabi-

printf '\nBuilt RAM helper images:\n'
sha256sum "$source_dir/utils/hwstub/stub/stmp/build/hwstub.sb" \
    "$source_dir/utils/hwstub/stub/stmp/build/hwstub.sb1"
printf '\nBuild complete. No helper has been sent to the player.\n'
