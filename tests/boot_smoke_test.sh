#!/usr/bin/env bash
# M1 CI gate: boots the kernel headlessly in QEMU and checks that the
# ZKT console banner appears on the serial port. See
# docs/FOUNDING-PROPOSAL.md §7, step 11.
#
# Usage: tests/boot_smoke_test.sh [path-to-kernel-elf]
set -euo pipefail

KERNEL="${1:-build/manios-zkt.elf}"
EXPECTED="Milestone M1: kernel console reached."
TIMEOUT_SECS=10

if [ ! -f "$KERNEL" ]; then
	echo "error: kernel image not found at $KERNEL (build it first: make)" >&2
	exit 1
fi

LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

timeout "${TIMEOUT_SECS}s" qemu-system-i386 \
	-kernel "$KERNEL" \
	-serial "file:$LOG" \
	-display none \
	-no-reboot -no-shutdown \
	-m 32 || true

if grep -qF "$EXPECTED" "$LOG"; then
	echo "PASS: ZKT kernel console banner found in serial output"
	exit 0
fi

echo "FAIL: expected banner not found in serial output within ${TIMEOUT_SECS}s"
echo "--- serial log ---"
cat "$LOG"
exit 1
