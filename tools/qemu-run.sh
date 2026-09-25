#!/usr/bin/env bash
# Boots the ZKT kernel in QEMU with the serial console attached to the
# terminal, and an NE2000 on QEMU's user-mode network (the guest is
# 10.0.2.15, the host 10.0.2.2). See docs/FOUNDING-PROPOSAL.md §4.3.
#
# Usage: tools/qemu-run.sh [path-to-kernel-elf [extra QEMU arguments...]]
#   e.g. tools/qemu-run.sh build/manios-zkt.elf -append "export=/boot"
set -euo pipefail

KERNEL="${1:-build/manios-zkt.elf}"
shift || true

if [ ! -f "$KERNEL" ]; then
	echo "error: kernel image not found at $KERNEL (build it first: make)" >&2
	exit 1
fi

# A 486 is QEMU's oldest CPU model and the closest to ZKT's i386 target.
exec qemu-system-i386 -kernel "$KERNEL" -cpu 486 -serial stdio -m 32 \
	-netdev user,id=n0 -device ne2k_isa,netdev=n0,iobase=0x300,irq=9 "$@"
