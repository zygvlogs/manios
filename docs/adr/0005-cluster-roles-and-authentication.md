# ADR-0005: Cluster roles as services and exports, and a shared-key authentication for ZRP

**Status:** Accepted; implemented at M13 ([notes](../milestones/M13-cluster-roles.md), [protocol](../zrp.md))
**Date:** 2026-09-25

## Context
ADR-0003 set the direction: ManiOS machines specialise as file servers,
CPU servers and terminals, sharing resources over ZRP, and deferred two
things to M13 — the roles themselves, and authentication ("ZRP v1 has no
authentication"). By M12 the pieces were there: per-process namespaces,
ZRP over UDP (M10), and programs that serve files (M12). What was
missing:

- **A way for a program on one machine to run with another machine's
  console and files** — Plan 9's `cpu`.
- **Requests that wait.** A remote shell reads the terminal's console,
  which may take minutes; a remote window waits for events. M10's server
  answered one request at a time, and its client gave up after 9.5 s.
- **Knowing who is asking.** Running programs for anyone who can send a
  datagram is not acceptable, even on a lab network.

## Decision
1. **Roles are configuration, not kinds of kernel.** Every ManiOS machine
   runs the same kernel with a ZRP server; what it does depends on what it
   exports and runs:
   - a **file server** exports storage (`export=/n/ata0p1` at boot);
   - a **CPU server** runs `cpud` (from a boot script, `rc=`), a
     userspace file server offering a clone file, `new`, to which a job
     is written;
   - a **terminal** is where a user sits; its `cpu` command runs a
     program on a CPU server.
2. **The terminal's namespace travels, as in Plan 9.** `cpu` exports its
   caller's namespace under a fresh name (new system call `SYS_EXPORT`;
   the export lasts as long as the process). The job on the CPU server
   mounts it at `/mnt/term`, binds the terminal's `/dev/cons` over its
   own, and the terminal's other devices *after* its `/dev` — so the
   program's console is the terminal's, the CPU server's own devices
   (`/dev/sysname`, `/dev/time`) stay its own, and a device only the
   terminal has — `/dev/wsys` inside the desktop — is the terminal's. A
   window opened by a program on the CPU server appears on the
   terminal's screen.
3. **The kernel's ZRP server works concurrently** — a receiver thread and
   worker threads started as needed — and a new reply, **`Rpending`**,
   answers a retransmission of a request still being worked on, so the
   client keeps waiting (asking again every second) instead of timing
   out. The client keeps **any number of requests in flight** over UDP,
   as it did over channels.
4. **Authentication by a shared cluster key** (`key=` at boot), mutual,
   by challenge and response: `Tauth` exchanges nonces and the server's
   proof, `Tattach` carries the client's; both are HMAC-SHA-256 of the
   nonces under the key. A machine with a key admits only clients that
   prove it, and trusts only servers that do. The protocol becomes
   **ZRP2**.

## Alternatives considered
- **A remote-execution protocol of its own** (like rsh). It would need
  its own way to carry the console, files and windows — everything ZRP
  already carries. Rejected: `cpu` is ~200 lines because it is only a
  namespace export, a mount and a clone file.
- **Binding the terminal's `/dev` before the CPU server's**, as Plan 9's
  `cpu` does. Then `/dev/sysname`, `/dev/time` and the rest would be the
  terminal's, and a program couldn't tell which machine it runs on
  without `#` device names (which ManiOS doesn't have). Binding only the
  console over, and the rest after, gives the useful part (console,
  windows) with less surprise.
- **Per-user keys and an authentication server** (Plan 9's factotum and
  authsrv). The right destination, but ManiOS has no users yet; one key
  per cluster is the smallest thing that stops strangers, and the
  exchange is shaped so that a key per user can replace it.
- **Signing every message.** It would stop a machine on the path from
  injecting requests into an authenticated session (which it can do by
  forging the client's address). Deferred: it changes every message, and
  replay protection would have to coexist with retransmission. Documented
  as a limit.
- **TCP instead of UDP with retransmission.** ZKT has no TCP; UDP with
  `Rpending` gives long waits without it.

## Consequences
- Every machine listens on UDP 5640. Without a key it accepts any client,
  as in M10; `cpud` therefore refuses to start on a machine without a key
  unless forced (`cpud -i`), and `/dev/zrp` says whether there is one.
- The same kernel serves every role, so a cluster can rearrange itself
  by changing boot lines.
- Remote programs see the terminal's windows only when `cpu` is run
  inside the desktop, whose namespace is the one exported — a direct
  consequence of namespaces being per process.
- Nonces must never repeat; ManiOS has no hardware randomness, so they
  come from a hash of the clock, the timer's position within its tick and
  a counter (unique, not secret — which is what the exchange needs).

## References
- ADR-0003 (namespaces, ZRP, the roles), ADR-0004 (window system as
  files), [docs/zrp.md](../zrp.md)
- Plan 9's `cpu(1)`, `exportfs(4)` and `authsrv(6)` — published design
  only.
- FIPS 180-4 (SHA-256), RFC 2104 (HMAC), RFC 4231 (test vectors).
