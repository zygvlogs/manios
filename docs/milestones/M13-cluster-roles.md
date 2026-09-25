# M13 — Cluster Roles: File Server, CPU Server, Terminal

**Status:** Achieved (2026-09-25). Implements
[FOUNDING-PROPOSAL.md §6](../FOUNDING-PROPOSAL.md#6-development-roadmap)
milestone M13, the roles ADR-0003 set out, with the decisions recorded
in [ADR-0005](../adr/0005-cluster-roles-and-authentication.md). The
protocol is now **ZRP2** ([docs/zrp.md](../zrp.md)).

Three machines sharing a key:

```
fs1    ip=10.0.0.1/24 key=SECRET sysname=fs1 export=/n/ata0p1
cpu1   ip=10.0.0.2/24 key=SECRET sysname=cpu1 rc=/boot/etc/rc.cpu
term1  ip=10.0.0.3/24 key=SECRET sysname=term1
```

On the terminal:

```
manios% mount udp!10.0.0.1 /n                   # the file server's disk
manios% cpu udp!10.0.0.2 cat /dev/sysname       # runs on the CPU server
cpu1
manios% cd /n; cpu udp!10.0.0.2                 # a shell there
manios% pwd
/mnt/term/n                                     # the terminal's namespace...
manios% sum frag.bin                            # ...with the file server in it
frag.bin: 20000 bytes, fnv1a 0f8a3c24
manios% exit
```

And inside the desktop, `cpu udp!10.0.0.2 clock` opens a clock window on
the terminal's screen, drawn by a program running on the CPU server.

## What M13 delivers

| Piece | Files | Summary |
|---|---|---|
| SHA-256, HMAC | `zkt/kernel/sha256.c` | FIPS 180-4 and RFC 2104, integer-only; known-answer tests at boot |
| Keys, nonces, proofs | `zkt/net/zrp_auth.c` | `key=` at boot; unique nonces without hardware randomness |
| ZRP2 server | `zkt/net/zrp_server.c` | Worker threads; `Rpending`; eight kept replies per session; named exports with their own namespaces; authentication; `/dev/zrp` |
| ZRP2 client | `zkt/net/zrp_client.c` | Any number of requests in flight over UDP too; `Rpending` keeps a waiting request alive; mutual authentication; a session whose server stopped answering fails fast |
| Exports | `SYS_EXPORT`, `export()`/`unexport()` | A process serves a directory of its own namespace under a name, until it exits |
| Devices | `zkt/drivers/sysname.c`, `zkt/net/netif.c`, `zrp_server.c` | `/dev/sysname` (`sysname=`), `/dev/net` (addresses), `/dev/zrp` (server state; whether there is a key, never the key) |
| Boot script | `rc=` (`zkt/kernel/monitor.c`) | A shell script run before the interactive shell: `/boot/etc/rc.cpu` starts `cpud` |
| CPU server | `userland/bin/cpud.c` | The `cpu` service: a clone file `new` and `N/wait`, exported as `cpu`; runs each job in the terminal's namespace |
| Terminal | `userland/bin/cpu.c` | `cpu HOST [COMMAND...]`: exports its caller's namespace, starts the job, waits, exits with its status |
| Kernel fix | `zkt/scheduler/sched.c` | A dying thread lets go of its namespace itself (see below) |
| Tests | `userland/test/cltest.c`, `tests/cluster_test.py` | At boot; and four machines plus the host on one network |

## Design decisions

The main ones are ADR-0005's: roles are configuration of one kernel; the
terminal's namespace travels (`/mnt/term`, its console bound over the CPU
server's, its other devices after); the kernel's server works
concurrently and says `Rpending`; a shared key authenticates both ends.
Further:

- **The job's namespace.** A job starts from `cpud`'s namespace, forks
  it, mounts the terminal's export at `/mnt/term`, binds
  `/mnt/term/dev/cons` over `/dev/cons` and `/mnt/term/dev` *after*
  `/dev`, opens `/dev/cons` as descriptors 0–2, and starts in
  `/mnt/term` plus the terminal's directory. A program named without a
  `/` is the CPU server's `/bin`.
- **`cpu` exports a copy of its namespace, then works in another copy.**
  Its own mount of the CPU service must not show up in what the job
  sees (an early version had the service tree at `/mnt/term/n` instead
  of the terminal's `/n` — the cluster test caught it).
- **Status travels as a number.** The job ends with the program's exit
  code, or 128 + the vector that killed it (and says so on the
  terminal's console), which `cpu` returns — as the shell reports it.
- **One idle worker.** The server starts one worker and adds more while
  requests outnumber idle workers (up to 16); a worker that finishes
  while another is idle exits. So a burst leaves nothing behind, which
  the boot self-tests check (threads, frames and heap all back to where
  they were).
- **Sessions end when they are empty.** UDP has no hang-up, so the
  server forgets a session once a `Tclunk` leaves it with no fids. A
  failed `Tattach` leaves the session for another try, but frees the
  replies kept for it (an early version ended the session there too,
  which refused a client's second, valid attach — `net_test` caught it).
- **A dying thread releases its namespace itself.** Before, the last
  reference to a thread's namespace was dropped in the scheduler, while
  switching away from the dead thread with interrupts off. A namespace
  with a remote mount in it sends `Tclunk` and waits when released —
  impossible there: the first `cpu` job froze the CPU server when it
  exited. `thread_exit` now drops the reference first, where waiting is
  allowed.
- **A dead server fails fast.** When a request times out, the client
  marks the session dead, so the clunks that follow as files close don't
  each wait another 9.5 s.
- **`cpud` wants a key.** Without one, anyone who can reach the machine
  could run programs; `cpud` refuses unless started with `-i`.
- **Clone files over the network.** The server opens each walked file
  for reading *and* writing when it can, so a write and a read of a
  clone file (a window's `new`, `cpud`'s `new`) reach the same open file
  through a remote mount.

## Verification

- **At every boot:**
  - SHA-256 and HMAC known-answer tests (FIPS 180-4 examples including
    a million `a`s fed in uneven pieces, RFC 4231 cases 1, 2 and 6).
  - The network self-test (M10) gained named exports (taken, invalid,
    a file, taken back by the wrong owner, fids that outlive their
    export, all of an owner's at once) and authentication on loopback: a
    keyed server admits the right key only; a keyed client refuses a
    server that can't prove the key.
  - **`cltest`** (27 checks): a process exports its namespace and mounts
    it back over UDP, reaching a file server that is itself a program
    over a pipe — three layers of the same protocol; a read waiting
    there is answered `Rpending` on repeats while another request on the
    same mount is answered; export names and owners; an export gone when
    its process exits; `/dev/sysname`, `/dev/net`, `/dev/zrp`. As for
    every boot test, no threads, frames or heap may be left over. The M13
    marker reports it; the 8 MiB machine still boots.
- **`tests/cluster_test.py`** (in `make test`, 10 checks): four machines
  and the host on a hub the test runs.
  - The file server's export, mounted by the terminal.
  - A machine with the wrong key refused by the file server and the CPU
    server.
  - The host's own ZRP2 client (Python's `hmac`): the right key works
    both ways; no proof, a proof without `Tauth`, a proof replayed from
    another session and the wrong key's proof are all `EACCES`, and the
    server's proof doesn't check out against a wrong key; a short
    `Tauth` is `EPROTO`; neither key nor passphrase ever appears in a
    datagram.
  - The host's own keyed server, mounted by the terminal (the host
    checks the terminal's proof); servers that answer `Tauth` with a
    wrong proof, or not at all, are refused.
  - `cpu` running a command on the CPU server, on the terminal's
    console.
  - A remote shell: in the terminal's directory under `/mnt/term`; a
    checksum of a file on the *file server*, read by the CPU server
    through the terminal's namespace; a 12-second wait, longer than
    UDP's timeout; `exit 7` reported back.
  - Exit statuses through the monitor's `run`: failure (1), a fault on
    the CPU server (142, and a message on the terminal), a missing
    program (127).
  - A program on the CPU server opening a window on the terminal's
    desktop: the remote clock's time read off the screen, ticking;
    `q` typed on the terminal's keyboard reaches it and it exits.
  - The CPU server powered off under a job: `cpu` reports the timeout
    and the terminal's shell carries on.
- `net_test` gained a protocol-level `Rpending` check from the host
  (a read of `/dev/mouse` through an export named `dev`: a repeat gets
  `Rpending`, another request is answered meanwhile, the reply comes
  when the mouse moves, and a repeat afterwards gets the same reply),
  and `Tauth` on a server without a key (`ENOSYS`). `console_test`
  gained `/dev/sysname`, `/dev/zrp`, `cpud` refusing without a key, and
  `cpu` without a network address.
- **Negative controls** (temporary sabotage, reverted afterward), each
  caught:

  | Sabotage | Caught by |
  |---|---|
  | The server accepts any client proof | The host client (a replayed proof accepted) |
  | The client trusts any server proof | The host's wrong-proof server was mounted |
  | No `Rpending` | `net_test`'s `Rpending` check |
  | The client ignores `Rpending` | The remote shell's 12-second wait |
  | Exports outlive their process | `cltest` at boot |
  | One worker thread only | Boot hangs in `cltest` |
  | A dying thread's namespace released in the scheduler (the old code) | The CPU server freezes after the first job |
  | A timed-out session stays alive | The terminal's prompt comes back too late after the CPU server dies |

  Two of these escape the boot self-tests by design — the kernel's own
  client can't tell a lax server (it refuses the wrong key first), and
  the boot test waits too briefly to need `Rpending` — which is what the
  independent host implementation and the network tests are for.

## Numbers

- `cpu` is 15 KB stripped, `cpud` 21 KB. The boot archive is 520 KB;
  the kernel image 1.2 MB.
- A session over UDP costs the server up to eight kept replies (at most
  1472 bytes each); a worker, a kernel stack and a 1472-byte buffer.

## Known limits

- Authentication is at attach only: messages are neither signed nor
  encrypted (docs/zrp.md "Security").
- One key per cluster; no users.
- A job can't be interrupted from the terminal (no signals yet); if the
  terminal goes away, the job's console reads fail and it usually ends.
- `cpu` passes arguments, not the shell's syntax: `cpu HOST sh -c '...'`
  for pipelines.
- A remote window's pixels cross the network uncompressed, 1472 bytes a
  datagram, one request at a time per write: a clock is fine; a
  terminal window repaints slowly.
- Every machine listens on UDP 5640.
- If the CPU server can't import the terminal's namespace, the job ends
  with status 125 and the reason is printed on the CPU server's console,
  not the terminal's.
