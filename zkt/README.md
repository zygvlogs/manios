# zkt/ — ZygKernel Technology

ManiOS's own kernel. See
[`docs/FOUNDING-PROPOSAL.md` §2](../docs/FOUNDING-PROPOSAL.md#2-zkt-zygkernel-technology-architecture)
for the full architecture and
[ADR-0002](../docs/adr/0002-kernel-architecture-style.md) for the
hybrid-kernel design decision.

## Layout

- `arch/<cpu>/` — the **only** place architecture-specific code
  (assembly, `#ifdef`, port I/O, CPU-specific structures) is allowed to
  live. Everything else in `zkt/` must stay architecture-neutral and
  reach hardware only through the HAL boundary this implies. First
  target: `arch/i386/`.
- `mm/` — physical memory management (bitmap allocator over the boot
  memory map), virtual memory management (i386 paging), kernel heap.
- `scheduler/` — processes, threads, run queues, context switching.
- `ipc/` — message-passing ports (send/receive/reply).
- `drivers/` — the driver framework (per-device-class interface) and
  in-tree drivers (serial, VGA text, keyboard, PIT, ATA/IDE, …).
- `fs/` — the VFS (vnode-style) abstraction and filesystem
  implementations.
- `kernel/` — boot handoff, `kernel_main()`, panic/logging, syscall
  dispatch — the glue that wires the other subdirectories together.

Empty until [the open decisions](../docs/FOUNDING-PROPOSAL.md#11-open-decisions-requiring-approval)
in the founding proposal are confirmed.
