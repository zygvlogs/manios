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

exec qemu-system-i386 -kernel "$KERNEL" -serial stdio -m 32
