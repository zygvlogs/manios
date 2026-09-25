#!/usr/bin/env bash
# Boots the ZKT kernel in QEMU with the serial console attached to the
# terminal. See docs/FOUNDING-PROPOSAL.md §4.3.
#
# Usage: tools/qemu-run.sh [path-to-kernel-elf]
set -euo pipefail

KERNEL="${1:-build/manios-zkt.elf}"

if [ ! -f "$KERNEL" ]; then
	echo "error: kernel image not found at $KERNEL (build it first: make)" >&2
	exit 1
fi

# A 486 is QEMU's oldest CPU model and the closest to ZKT's i386 target.
exec qemu-system-i386 -kernel "$KERNEL" -cpu 486 -serial stdio -m 32
