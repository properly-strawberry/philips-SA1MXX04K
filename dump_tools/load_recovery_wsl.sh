#!/usr/bin/env bash
# Send the pinned Rockbox helper to ROM RAM using the upstream Linux USB loader.
# Requires the 066f:3700 device to be attached to WSL by the user first.
# The helper waits for host requests; it does not read or program flash on startup.
set -euo pipefail

task_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
image_file="$task_dir/rockbox/utils/hwstub/stub/stmp/build/hwstub.sb1"
loader_file="$task_dir/rockbox/utils/imxtools/sbtools/sbloader"
log_file="$task_dir/load_recovery_wsl.log"
exec > >(tee "$log_file") 2>&1

if [[ "$EUID" -ne 0 ]]; then
    printf 'Run this script with sudo so libusb can open the recovery interface.\n'
    exit 1
fi
test -x "$loader_file"
printf '%s  %s\n' \
    f818bfae06e439156dc0bdaac8a542a4bf859c02703de6fda524076b2ddcc512 \
    "$image_file" | sha256sum --check --strict -

printf 'Sending the 9216-byte helper to STMP3700 ROM RAM.\n'
"$loader_file" -d -u 066f:3700 -p hid -x 64 "$image_file"
printf '\nLoader returned. Its status message and USB re-enumeration must be checked.\n'
# Upstream sbloader does not propagate send_hid() failure to its process exit code.
# Therefore a zero exit status alone must never be treated as proof of success.
