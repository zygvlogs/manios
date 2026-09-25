#!/usr/bin/env bash
# CI gate: boots the kernel headlessly in QEMU under several machine
# configurations and checks the serial console for each milestone's
# marker, the final prompt, and the absence of a panic.
#
# Usage: tests/boot_smoke_test.sh [path-to-kernel-elf]
set -euo pipefail

KERNEL="${1:-build/manios-zkt.elf}"
TIMEOUT_SECS=15
PROMPT="ZKT> "
EXPECTED=(
	"Milestone M1: kernel console reached."
	"Milestone M2: memory manager online (self-test passed)."
	"Milestone M3: interrupts online (PIT timer at 100 Hz)."
	"Milestone M4: kernel threads online (cooperative + preemptive scheduling, self-test passed)."
	"Milestone M5: driver framework online (keyboard + serial console input, mutex self-test passed)."
	"Milestone M6: ATA storage driver online."
)

# "<RAM MiB> <QEMU CPU model> <why>". QEMU's oldest model is the 486;
# it faults on post-386 instructions such as CMOV, which is what keeps
# the -march=i386 build honest. A 486 has no PAE, so the >4 GiB case
# needs a newer model.
CASES=(
	"8 486 small old-hardware machine"
	"256 486 typical memory size"
	"4096 qemu32 RAM above 4 GiB must be ignored"
)

if [ ! -f "$KERNEL" ]; then
	echo "error: kernel image not found at $KERNEL (build it first: make)" >&2
	exit 1
fi

LOG="$(mktemp)"
QEMU_ERR="$(mktemp)"
trap 'rm -f "$LOG" "$QEMU_ERR"' EXIT

run_case() {
	local mem="$1" cpu="$2"
	: >"$LOG"
	qemu-system-i386 -kernel "$KERNEL" -cpu "$cpu" -m "$mem" \
		-serial "file:$LOG" -display none -no-reboot 2>"$QEMU_ERR" &
	local pid=$!
	local deadline=$((SECONDS + TIMEOUT_SECS))
	while ((SECONDS < deadline)) && kill -0 "$pid" 2>/dev/null; do
		if grep -qF -e "$PROMPT" -e "ZKT PANIC" "$LOG"; then
			break
		fi
		sleep 0.2
	done
	if ! kill -0 "$pid" 2>/dev/null; then
		# With -no-reboot, a triple fault makes QEMU exit instead of reset.
		echo "(QEMU exited on its own: triple fault, or failed to start)" >>"$QEMU_ERR"
	fi
	kill "$pid" 2>/dev/null || true
	wait "$pid" 2>/dev/null || true

	grep -qF "ZKT PANIC" "$LOG" && return 1
	for line in "${EXPECTED[@]}" "$PROMPT"; do
		grep -qF "$line" "$LOG" || return 1
	done
	return 0
}

failed=0
for c in "${CASES[@]}"; do
	read -r mem cpu why <<<"$c"
	if run_case "$mem" "$cpu"; then
		echo "PASS: ${mem} MiB, cpu=${cpu} (${why})"
	else
		echo "FAIL: ${mem} MiB, cpu=${cpu} (${why})"
		echo "--- serial log ---"
		cat "$LOG"
		echo "--- qemu stderr ---"
		cat "$QEMU_ERR"
		echo "-------------------"
		failed=1
	fi
done
exit "$failed"
