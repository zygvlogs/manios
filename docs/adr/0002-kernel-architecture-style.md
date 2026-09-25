# ADR-0002: Hybrid kernel — monolithic performance, microkernel-shaped boundaries

**Status:** Accepted
**Date:** 2026-09-25

## Context
ZKT's overall kernel style is the single largest architectural fork in
the road for ManiOS. The founding prompt lists IPC and security
boundaries as core kernel responsibilities (suggesting isolation
matters) while also targeting older, low-resource i386 hardware and
asking for a lightweight, modern design (suggesting microkernel-style
message-passing overhead on every operation is not affordable).

## Decision
ZKT is a **hybrid kernel**: performance-critical subsystems (scheduler,
VMM, VFS dispatch, core IPC) run in kernel space for a monolithic-like
performance profile; drivers are written against a defined driver
framework interface (in-kernel initially, retrofittable to userspace
servers later); IPC is a real, first-class kernel primitive from the
start rather than bolted on after userspace exists; security boundaries
in the first milestones rely on hardware-provided ring 0/ring 3
separation, not a software capability system.

## Alternatives considered
- **Pure microkernel** (seL4-style: scheduler, VMM, IPC in kernel;
  everything else — drivers, filesystems, network stack — as userspace
  servers from day one). Rejected for the first milestones: message-
  passing overhead on every driver/filesystem operation is a real cost
  on i386-class hardware, and building the userspace-server machinery
  (M8's concern) before there is even a kernel console (M1) inverts the
  project's own incremental roadmap.
- **Traditional fully-monolithic kernel** (drivers, filesystems,
  network stack all directly in kernel space with no isolation
  boundary or driver-framework discipline, e.g. early Linux/BSD style).
  Rejected: forecloses the option of moving components to userspace
  later without a rewrite, which conflicts with the founding prompt's
  requirement that "individual subsystems can evolve without rewriting
  the entire operating system."

## Consequences
- Drivers must be written against the framework interface (§2.7 of the
  founding proposal) even while living in-kernel — a small discipline
  tax on every driver, paid to keep the userspace-server option open.
- IPC (§2.8) is designed and implemented before it has real external
  consumers (userspace doesn't exist until M8) — deliberate, so its
  shape isn't retrofitted around one caller's needs.
- Security model stays intentionally minimal (ring 0/3 only) through
  M9; a capability or sandboxing layer is out of scope until named
  explicitly in a future ADR.

## References
- `docs/FOUNDING-PROPOSAL.md` §1.2 (Kernel design philosophy), §2 (ZKT
  Architecture), §11 (D1)
