# ADR-0003: Plan 9-inspired per-process namespaces and a uniform resource protocol (ZRP), for cluster transparency

**Status:** Accepted. Namespaces implemented at M7/M8; ZRP wire format v1 and `mount` at M10 ([docs/zrp.md](../zrp.md))
**Date:** 2026-09-25

## Context
The project wants ManiOS to grow into a **cluster-capable OS in the Plan 9
sense**: multiple machines (or VMs/containers) sharing resources —
storage, CPU, devices — transparently over the network, with no special
casing between "local" and "remote". Plan 9's central trick for this is
architectural, not a bolted-on distributed filesystem:

1. **Everything is reached through a file-like namespace** — devices,
   network connections, running processes, and remote resources all
   present themselves as things you `open`/`read`/`write`/`walk`, not as
   bespoke syscalls or ioctls per resource type.
2. **Namespaces are per-process, not global.** Each process has its own
   view of "the filesystem", built by `bind`ing and `mount`ing resources
   into it. Two processes can see completely different worlds built from
   the same underlying servers.
3. **One protocol (9P) carries all of this**, whether the resource is a
   local driver or a service on another machine across the network — the
   same request/response messages, over different transports (a local
   channel vs. a network connection). This is *why* remote resources look
   local: the client-side code path is identical either way.
4. **Machines specialize by role** — CPU servers (compute), file servers
   (storage), and terminals (thin clients that import both over the
   network) — rather than every machine being a self-contained silo.

This is a substantial addition to what was scoped in the original
founding proposal (§1–§2), but it reshapes specific later layers rather
than the whole architecture: it mainly affects VFS (layer 8), IPC (layer
7), and networking (layer 10) — milestones M7, M8, and M10 — not the
boot/memory/interrupt/scheduling work already planned for M1–M6.

## Decision
Adopt the Plan 9 model as the target shape for ManiOS's VFS, IPC, and
networking layers, implemented as ManiOS's own original protocol and
namespace design (BSD-inspired-style originality per
[§3.2](../FOUNDING-PROPOSAL.md#32-two-different-things-bsd-inspired-vs-bsd-derived)
of the founding proposal — no Plan 9 (Plan 9 is MIT-licensed, not
BSD-licensed, and out of scope for the BSD-integration strategy anyway)
source is imported; only the well-published design is used as a
reference):

- **ZRP (ZygKernel Resource Protocol)** — ManiOS's own 9P-inspired,
  transport-agnostic message protocol (walk/open/read/write/clunk-style
  operations) for reaching any resource — a local driver, a local server
  process, or a resource on a remote ManiOS node. Local IPC ports
  (§2.8) become one ZRP transport; network connections (M10) become
  another. This is the mechanism that makes "cluster" transparent: the
  same client code walks a namespace whether the thing at the end of the
  walk is local or across the network.
- **Per-process namespaces**: each process gets its own mount/bind table
  (inherited from its parent at fork/spawn time, then independently
  mutable), rather than one global filesystem tree. Union directories
  (multiple resources bound at one point, searched in order) are part of
  this from the start, not a later addition.
- **Devices as ZRP-reachable resource trees**: the driver framework
  interface already sketched in
  [§2.7](../FOUNDING-PROPOSAL.md#27-driver-framework) (open/read/write
  per device) maps directly onto this — a driver exposes itself as a
  small file tree rather than a set of ioctls, so no rework of that
  layer's shape is needed, only of what sits above it.
- **Cluster roles as a post-M10 milestone (M13)**: file-server, CPU-
  server, and terminal roles for a ManiOS node — i.e. a ManiOS machine
  can export its storage or its CPU/process-execution over ZRP to other
  ManiOS nodes, and a thin "terminal" node can import both. This is
  explicitly sequenced after userspace (M8) and networking (M10) exist,
  not before.

## Alternatives considered
- **Bespoke IPC/RPC per subsystem** (a syscall for network sockets, a
  different one for device ioctls, another for process control) — the
  conventional BSD/POSIX-ish approach already implicit in the original
  §2.8–§2.10 sketch. Rejected as the long-term shape: it doesn't give
  cluster transparency "for free" the way one uniform protocol does — an
  RPC mechanism would have to be retrofitted later on top of already-
  divergent per-subsystem interfaces, exactly the kind of rework the
  project's own HAL discipline (§1.3) exists to avoid.
- **A global filesystem namespace (single mount table for the whole
  system, as in traditional Unix/BSD)** — rejected as the default,
  because it forecloses the per-process-namespace flexibility that makes
  "import a remote machine's resources into just this process/session"
  possible without a global remount. ManiOS's VFS (§2.10) keeps its
  BSD-inspired vnode *dispatch* mechanism underneath — this decision is
  about namespace *structure* above it, not a rejection of §2.10's
  vnode-operations design.

## Consequences
- VFS (M7) is designed around per-process mount tables and union
  directories from the start, not retrofitted after a global-namespace
  VFS ships.
- IPC (§2.8, already targeted at M1-adjacent milestones as a first-class
  primitive with no early consumers) gets its shape informed by ZRP
  earlier than it otherwise would have — its message format should be
  compatible with becoming a ZRP transport, even before ZRP itself is
  implemented at M10.
- Networking (M10) is scoped to include the ZRP-over-network transport,
  not just a generic BSD-sockets-style stack; a conventional socket API
  (§9 of the original roadmap discussion) may still exist as a
  convenience layer on top, but ZRP is the substrate.
- A new milestone, **M13 — cluster roles (file server / CPU server /
  terminal)**, is added after M10 (networking) and depends on M8
  (userspace) and M10.
- Naming, wire format details, and the exact namespace-manipulation
  syscalls (Plan 9's `bind`/`mount`/`unmount`/`rfork` equivalents) are
  **not** finalized by this ADR — they are designed when M7/M8/M10
  actually start, informed by real constraints discovered building M1–M6
  first. This ADR fixes the *direction*, not the wire format.

## References
- `docs/FOUNDING-PROPOSAL.md` §1.1 (layering), §2.7 (driver framework),
  §2.8 (IPC), §2.10 (VFS), §6 (roadmap)
- Plan 9 from Bell Labs — public, well-documented OS design (9P
  protocol, namespaces, union directories, CPU/file/terminal server
  model). Referenced for its published design only; no source imported.

## Implementation status (M7)

Per-namespace mount tables with replace/before/after binds, union
directories, lexical path cleaning, and namespace forking landed at M7.
See [the M7 notes](../milestones/M7-vfs-namespaces.md). Namespaces
belong to threads until processes exist at M8. The vnode operations
(walk / read / write at an offset / readdir / release) are shaped for a
one-to-one mapping onto ZRP messages at M10.

## Implementation status (M8)

User processes reach namespaces through the `bind`, `unbind` and
`nsfork` system calls ([M8 notes](../milestones/M8-userspace.md)). A
spawned child shares its parent's namespace, as with `rfork` without
`RFNAMEG`; `nsfork` is `RFNAMEG`. The boot archive is mounted at
`/boot` and `/bin` is a bind of `/boot/bin`, so a disk's `bin/` can be
unioned in front of it or behind it.

## Implementation status (M10)

ZRP version 1 is specified in [docs/zrp.md](../zrp.md), and runs over
UDP with Plan 9 IL-style retransmission
([M10 notes](../milestones/M10-networking-zrp.md)). The kernel has a
server, which exports a directory of the namespace that started it, and
a client, which is a filesystem. The `mount(dial, old, flag, aname)`
system call attaches to a server and binds its tree as `bind` does. Two
ManiOS machines can now read each other's files and run each other's
programs. Authentication, and the cluster roles themselves, remain for
M13. Local IPC as a ZRP transport is not built yet: servers are kernel
threads, reached over UDP even locally (loopback).

## Note: mounts keyed by path

One deliberate simplification: the mount table is keyed by cleaned
*path*, not by the identity (qid) of the bound-over file as in Plan 9.
With lexical `..` this differs only when a bound-over directory is
reachable by two paths. Revisit when ZRP introduces qids.
