# ADR-0004: The window system is a file server reached over ZRP

**Status:** Accepted; implemented at M12 ([design](../desktop/DESIGN.md), [notes](../milestones/M12-desktop.md)); windows sized by the desktop since M18 ([ADR-0007](0007-manide-tiling-desktop.md))
**Date:** 2026-09-25

## Context
M12 needs a way for applications to get windows, draw in them and
receive input. The kernel has per-process namespaces, a 9P-style
protocol (ZRP, ADR-0003) with a network transport, and — new at M12 —
pipes. It has no shared memory between processes and no message-passing
primitive beyond pipes. The target machines are small (i386, no FPU, tens
of megabytes). ADR-0003 wants resources reachable uniformly, locally or
remotely.

## Decision
The compositor (`desktop`) is an ordinary user program that **serves a
file tree, `/dev/wsys`, over ZRP on a pipe**, which the kernel mounts with
a new system call, `SYS_MOUNTFD`. A window is a directory (`ctl`, `image`,
`event`); a window is made by writing to a clone file, `new`, and lives
while that file is open. The kernel gains a channel transport for its ZRP
client that keeps any number of requests in flight, so reads the server
answers later (events) block no one else. Applications use `libwin`,
which is a thin layer over `open`/`read`/`write`.

## Alternatives considered
- **Window system calls in the kernel** (create window, blit, get event).
  Rejected: puts policy and a second protocol into ZKT, and a window could
  never be reached from another node.
- **Shared-memory surfaces with a message queue** (the Wayland shape).
  Faster for large repaints, but ZKT has neither shared memory nor a
  message queue; adding both for the MVP is a large kernel change, and the
  result is not reachable over the network. It remains an option for a
  later, faster image path.
- **A bespoke protocol over pipes.** The same plumbing without the
  benefits: `cat`, `echo`, `ls` and namespaces would not work on windows,
  and M13 could not export them.

## Consequences
- Any program can now serve files (`libc/zrpsrv.c`); the window system is
  the first of many possible user file servers.
- Pixel bandwidth is bounded by copying through the kernel; the MVP keeps
  windows small and sends only changed rows. A server-side drawing
  protocol is the planned follow-up.
- Because windows are files in a namespace, M13's CPU server can give a
  remote program a window by importing the terminal's `/dev/wsys` —
  nothing window-specific is needed for that.
- A program that must wait on several sources uses a helper process per
  blocking read (as Plan 9's libevent); `poll` only knows about local
  files.

## References
- ADR-0003 (namespaces and ZRP), [docs/zrp.md](../zrp.md)
- [docs/desktop/DESIGN.md](../desktop/DESIGN.md)
- Plan 9's rio and 8½ (window systems as file servers) — published
  design only; no source used.
