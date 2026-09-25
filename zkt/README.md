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
- `net/` — Ethernet/ARP, IPv4, ICMP, UDP, and ZRP, the resource protocol
  (ADR-0003).
- `kernel/` — boot handoff, `kernel_main()`, panic/logging, processes,
  system calls, the ELF loader — the glue that wires the other
  subdirectories together.
- `abi/` — `zkt_abi.h`, the system call ABI: the one header the kernel
  and userspace (`libc/`) share.

Generic code includes architecture headers by name (`"memlayout.h"`,
`"cpu.h"`, …) through the per-architecture include path the Makefile
sets, never by `../arch/<cpu>/` path, so another architecture can
supply headers of the same names.

Implemented so far: M1 (boot, console, exceptions), M2 (memory
management, [notes](../docs/milestones/M2-memory-management.md)), M3
(hardware IRQs and timer, [notes](../docs/milestones/M3-interrupts-and-timer.md))
M4 (kernel threads, [notes](../docs/milestones/M4-multitasking.md)) and
M5 (driver framework, [notes](../docs/milestones/M5-driver-framework.md)) and
M6 (ATA storage, [notes](../docs/milestones/M6-ata-storage.md)), M7
(VFS and namespaces, [notes](../docs/milestones/M7-vfs-namespaces.md))
M8 (userspace, [notes](../docs/milestones/M8-userspace.md)), M9 (libc and
shell, [notes](../docs/milestones/M9-libc-shell.md)) and M10 (networking and
ZRP, [notes](../docs/milestones/M10-networking-zrp.md)).
`ipc/` is still empty.
