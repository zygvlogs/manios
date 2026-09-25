# M5 — Driver Framework, Keyboard, Interactive Console

**Status:** Achieved (2026-09-25). Implements
[FOUNDING-PROPOSAL.md §2.7](../FOUNDING-PROPOSAL.md#27-driver-framework)
and roadmap M5.

## What M5 delivers

| Piece | Files | Summary |
|---|---|---|
| Device registry | `zkt/drivers/device.c`, `device.h` | Named devices with a character or block operations table |
| Serial | `zkt/drivers/serial.c` | COM1: polled early/panic path, plus IRQ-driven RX and buffered TX (4 KiB ring) |
| Keyboard | `zkt/drivers/ps2kbd.c` | PS/2 on IRQ1: scancode set 1, US layout, Shift/Caps Lock/Ctrl |
| VGA | `zkt/drivers/vga_text.c` | Hardware cursor, `\b` `\r` `\t`; device `vga` |
| Console | `zkt/kernel/kconsole.c` | Device `cons` (Plan 9's `/dev/cons`): input from keyboard and COM1, output to VGA and COM1 |
| Blocking primitives | `zkt/scheduler/sched.c`, `mutex.c` | Wait queues (wakeable from IRQs), a sleeping mutex |
| Kernel monitor | `zkt/kernel/monitor.c` | The `ZKT>` prompt: `help`, `echo`, `uptime`, `mem`, `threads`, `devices` |
| Support | `kprintf.c`, `kerrno.c`, `ring.h`, `kstring.c` | printf subset, error codes, byte rings, string functions |
| Interactive test | `tests/console_test.py` | Drives the monitor over serial and the PS/2 keyboard |

## Design decisions

- **Two device classes, chosen for M7.** Character devices are byte
  streams (`read` blocks until data arrives); block devices transfer
  numbered blocks. That's enough for devfs (M7) to expose every device
  as a file, which is ADR-0003's "devices as resource trees", with no
  per-device ioctls. Missing operations are NULL and come back as
  `-ENODEV`. Network and display classes wait for M10 and M11.
- **Console input is one queue with two feeders.** The keyboard and
  COM1 IRQ handlers both push into the console queue, and `cons` reads
  from it, as Plan 9's console does. The keyboard has no device of its
  own, since the console is its only consumer.
- **Serial output is buffered and interrupt-driven**, which resolves
  the M4 tradeoff where a long message held interrupts off while the
  polled UART drained. A thread now copies its message into a 4 KiB
  ring and the TX-empty interrupt feeds the UART. Only a 16550A gets
  16 bytes per interrupt; an 8250/16450 (or the buggy original 16550)
  gets one. The FIFO type is detected at init, which matters for old
  hardware.
- **Output takes two paths.** A thread that can block takes the console
  mutex, so messages stay whole but ticks keep running. Early boot, IRQ
  handlers, code already running with interrupts off, and panics can't
  block: they write synchronously with interrupts off, and first
  flush whatever is still buffered, so output never reorders.
- **Wait queues are condition-variable style.** The caller checks the
  condition and sleeps with interrupts off, so a wakeup can't be lost
  between the check and the sleep. `waitq_sleep` panics if interrupts
  are on. Waking a thread while idle runs switches to it immediately
  (inside the IRQ, the same mechanism as preemption) instead of waiting
  up to one tick.
- **The monitor is a kernel debugging console, not the shell.** The
  ManiOS shell is userspace (M9). The monitor exists to inspect a live
  kernel, including on real hardware with only a keyboard and screen,
  and to give tests a way to drive the kernel. M6 and M7 add commands
  to it.
- **`kprintf` is 32-bit only**: 64-bit division would pull in libgcc,
  which is built for i686. Block counts are therefore 32-bit, which
  covers 2 TiB at 512 bytes per sector.

## Verification

- `make test` now runs `tests/console_test.py` after the boot smoke
  test. The console test boots QEMU with the serial line on its stdio,
  and the QEMU monitor on a unix socket for `sendkey`. It checks:
  - serial input: `help`, `echo`, `devices`, and an unknown command;
  - PS/2 keyboard input: `uptime` and `threads`;
  - Shift mapping: `echo Hello, World! (x_y)` covers `!`, `,`, `(`,
    `)` and `_`;
  - backspace: `uptimx⌫e` must echo the `\b \b` erase sequence and run
    `uptime`.
- The boot smoke test requires the new M5 marker, printed after a mutex
  self-test. In that test two threads each read a shared counter,
  *yield*, then write it back plus one, under the lock. The forced
  yield means any lapse in mutual exclusion loses updates every time,
  not just occasionally.
- **Negative controls** (temporary sabotage, reverted afterward):
  - lock removed from the test's critical section → *mutex_unlock: not
    held by this thread*;
  - lock and unlock both removed → *mutex did not serialize a
    read-modify-write*;
  - the serial TX interrupt never enabled → output stops right after
    the switch to buffered mode, which proves the M5 marker and
    everything after it really travel the interrupt-driven path.
- The first run of the console test failed three cases. Two were bugs
  in the test's own expectations; the third was a typo in its keystroke
  script. The kernel output was correct each time.

## Known limits

- Keyboard: US layout only. Arrows and function keys are ignored, and
  there are no LEDs (Caps Lock works but its light doesn't change).
- Only COM1, and no way to change the baud rate at runtime.
- QEMU's UART drains instantly, so the "ticks keep running while a long
  message drains" improvement can only be shown on real hardware (or a
  throttled UART); in QEMU the tests prove only that output travels the
  interrupt path.
