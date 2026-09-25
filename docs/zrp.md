# ZRP — the ZygKernel Resource Protocol, version `ZRP2`

ZRP is how one ManiOS node reaches a file tree on another
([ADR-0003](adr/0003-plan9-namespaces-and-resource-protocol.md)). It is
ManiOS's own protocol, modelled on Plan 9's 9P but not wire-compatible
with it. Its messages map one-to-one onto the kernel's vnode operations:

| Vnode operation | ZRP request |
|---|---|
| walk | `Twalk` |
| read | `Tread` |
| write | `Twrite` |
| readdir | `Tread` on a directory |
| release | `Tclunk` |

A remote tree is therefore just another filesystem, bound into a
namespace like any other.

Three independent implementations follow this document: the kernel's
(`zkt/net/zrp_*.c`), the test suite's (`tests/net_test.py`,
`tests/cluster_test.py`), and, for servers that are programs,
`libc/zrpsrv.c` (M12). Each is tested against another.

**Versions.** `ZRP1` (M10) was the protocol below without
authentication, without `Rpending`, and with one request outstanding per
session. `ZRP2` (M13, [ADR-0005](adr/0005-cluster-roles-and-authentication.md))
adds those three; a `ZRP1` peer is refused at `Tversion`.

## Transport

ZRP runs over **UDP, port 5640**, one message per datagram. A
message is at most **1472 bytes**, the largest UDP payload a single
Ethernet frame carries, so IP never fragments. A client is identified
by its IP address and UDP port; that pair is a *session*.

**Reliability.** UDP may lose, duplicate or reorder datagrams, so ZRP
adds its own reliability, in the style of Plan 9's IL:

- A client may have **any number of requests outstanding** per session,
  each with its own tag (`ZRP1`: one).
- It **retransmits** each request byte for byte, with the same tag,
  until a reply with that tag arrives. The kernel waits 150 ms, then
  doubles the wait, for six tries (about 9.5 s in all), then reports
  `ETIMEDOUT` — and treats the session as dead: its other requests, and
  later ones, fail at once with `EIO`.
- A server that receives a retransmission of a request **it is still
  working on** answers **`Rpending`** (type 65, empty body). The client
  then keeps asking, once a second, for as long as the server keeps
  answering `Rpending`; so a request may wait as long as it needs (a
  console waiting for a line, a window waiting for an event), while a
  server that goes away is noticed within six seconds.
- A client **ignores replies whose tag isn't one it is waiting for**.
  These are late copies of earlier replies.
- A server **keeps each session's recent replies** (the kernel: eight).
  If the identical request arrives again, with the same tag, the server
  resends the saved reply without performing the request a second time.
  A repeated `Twalk` or `Tclunk` must not fail just because the first
  copy already succeeded.
- UDP has no hang-up: the kernel forgets a session once a `Tclunk`
  leaves it without fids (a failed `Tattach` only drops its kept
  replies), and replaces the least recently used idle session when all
  16 are taken.
- Tags count up, wrapping before 0xFFFF, skipping tags in flight. 0xFFFF
  (`NOTAG`) is reserved for `Tversion`.
- The kernel's server answers requests from worker threads, so one that
  waits holds up no other.

### Local channels (M12)

A program can serve files too: it reads requests from, and writes
replies to, one end of a pipe, and the kernel mounts the other end
(`SYS_MOUNTFD`, `mountfd()`). The desktop's window system is such a
server ([ADR-0004](adr/0004-window-system-as-a-file-server.md)). The
messages are the same; the transport differs:

- A pipe keeps **message boundaries**: one write is one message, and a
  reader with a large enough buffer gets it whole.
- Nothing is lost, duplicated or reordered, so there is **no
  retransmission** and no reply cache.
- A client may have **any number of requests outstanding**, each with
  its own tag. The server may answer them in any order, and may hold a
  read until it has something to say (a window's events). In the
  kernel, whichever waiting thread is receiving reads the next reply
  and hands it to the request with that tag.
- `msize` is **16384** (the client asks for it in `Tversion`; the
  server may lower it).
- When the server's end closes, every request waiting and every later
  one fails with `EIO`. When the kernel drops its end (the last
  reference to the mount is gone), the server reads end of file, by
  which time the kernel has sent a `Tclunk` for every fid it held.

## Messages

Every message is:

```
size[4] type[1] tag[2] body...
```

Integers are little-endian. `size` counts the whole message, header
included, and must equal the datagram's length. A server sends no
reply to a datagram whose `size` doesn't match. A string `s` is
`length[2]` bytes, with no terminator.

Some messages carry a **stat**, which describes a file:

```
type[1] length[4] name[s]
```

`type` is 0 for a directory, 1 for a file, 2 for a device (the
`ZKT_TYPE_*` values of `zkt_abi.h`). A file's name is its last path
element, and the root is `/`.

| Type | Request | Reply (type + 1) |
|---|---|---|
| 1 `Tversion` | `msize[4] version[s]` | `msize[4] version[s]` |
| 3 `Tattach` | `fid[4] aname[s]` | stat of the export's root |
| 5 `Twalk` | `fid[4] newfid[4] name[s]` | stat of `newfid` |
| 7 `Tread` | `fid[4] offset[4] count[4]` | `count[4] data` |
| 9 `Twrite` | `fid[4] offset[4] count[4] data` | `count[4]` |
| 11 `Tclunk` | `fid[4]` | (empty) |
| 13 `Tstat` | `fid[4]` | stat |
| 15 `Tauth` | `cnonce[16]` | `snonce[16] proof[32]` |
| — | | 64 `Rerror`: `errno[2]` |
| — | | 65 `Rpending` (UDP only; see Transport) |

- **`Tversion`** starts, or restarts, a session and drops all its fids.
  The version string must be `ZRP2`. The server replies with the
  smaller of its own `msize` and the client's, and neither side may
  then send a longer message. The minimum `msize` is 256. Every other
  request before `Tversion` gets `EPROTO`.
- **`Tauth`** begins authentication (below); it follows `Tversion`.
- **`Tattach`** makes `fid` refer to the root of the export named
  `aname`, and, after `Tauth`, ends with the client's proof:
  `fid[4] aname[s] proof[32]`. The empty name is the default export
  (`export=` at boot); a process names its own (`SYS_EXPORT`, M13).
- **`Twalk`** makes `newfid` refer to the entry `name` inside the
  directory `fid`, or to the same file as `fid` when `name` is empty.
  Walks are one step at a time. `name` may not contain `/`, and may not
  be `.` or `..`. Clients clean paths first, and a server must never
  let a walk climb out of its export. A `newfid` that is already in use
  is `EINVAL`.
- **`Tread`** on a file returns up to `count` bytes from `offset`; 0
  bytes means end of file. On a directory, `offset` counts *entries*,
  not bytes, and the data is a sequence of whole stat records. A reader
  asks for the next index after the last entry it received, and gets
  an empty reply at the end. Replies never exceed `msize`.
- **`Twrite`** writes `count` bytes at `offset` and returns how many it
  wrote. A file the server's namespace can't write gives an error such
  as `EROFS`.
- **`Tclunk`** forgets a fid. Fids are 32-bit numbers the client
  chooses, and a server may limit how many a session holds (the kernel
  allows 64: `EMFILE`).
- **`Rerror`** carries a ZKT error number (`zkt_abi.h`), which the
  client returns as the result of the failed operation. For example:
  - `ENOENT` 2: no such name, or no export by that name;
  - `EACCES` 13: authentication failed;
  - `EBADF` 9: unknown fid;
  - `EINVAL` 22: bad name, or fid in use;
  - `EROFS` 30: not writable;
  - `ENOSYS` 38: `Tauth` to a server without a key;
  - `EPROTO` 71: malformed request, unknown type, or no session.

Every field a server receives is validated before use, and a client
checks every reply the same way. A reply whose `count` exceeds its data,
or a stat with an unknown type, makes the operation fail with `EPROTO`;
it never crashes or corrupts the client.

## Authentication

Machines of a cluster share a key, `K` = SHA-256(`"ZRP2 key"` ‖
passphrase), the passphrase given at boot as `key=`. It never crosses
the wire. Over UDP, a client with a key:

1. sends `Tauth` with a fresh 16-byte `cnonce`;
2. gets `Rauth` with the server's fresh `snonce` and its proof,
   HMAC-SHA-256(K, `"ZRP2 server"` ‖ cnonce ‖ snonce), which it checks —
   a server that can't prove the key is refused (`EACCES`), as is one
   that answers `Tauth` with `Rerror`;
3. sends `Tattach` ending with its own proof, HMAC-SHA-256(K,
   `"ZRP2 client"` ‖ cnonce ‖ snonce).

A server with a key refuses (`EACCES`) any `Tattach` without the right
proof for this session's nonces — so a proof recorded from another
session is useless — and a new `Tversion` forgets the nonces. A server
without a key answers `Tauth` with `ENOSYS` and accepts every attach, as
`ZRP1` did. Local channels (above) are never authenticated: the kernel
itself is at the other end. Nonces come from a hash of the wall clock,
uptime, the timer's position within its tick and a counter: never
repeated, though not secret (ManiOS machines have no hardware
randomness).

## Security

- With a key, only machines that know it can attach — to read files,
  or to reach a CPU server's `cpud`. Without one, anyone who can send
  UDP to port 5640 can read everything exported, as with NFS before
  Kerberos; `cpud` therefore refuses to run on a machine without a key
  unless forced.
- Authentication happens once, at attach. Messages are **neither signed
  nor encrypted**: a machine on the path can read them, and one that
  forges the client's address can inject requests into an
  authenticated session. Use ZRP on networks where that is acceptable.
- While `cpu` runs, the terminal exports its caller's namespace to every
  machine holding the key.

## Differences from 9P

- **No open.** Reads and writes work directly on walked fids; the
  server opens files as it needs to.
- **One name per `Twalk`.** The reply is a stat, not a list of qids,
  so the client learns the type and size in the same round trip.
- **No qids.** Mount points are keyed by path; see ADR-0003.
- **Directory reads count entries, not bytes.**
- **Errors are numbers, not strings.**
- **Reliability over UDP is part of the protocol** (above), where 9P
  assumes a reliable transport; `Rpending` has no 9P counterpart.
- **Authentication is a message exchange in the protocol itself**
  (`Tauth`, then the proof in `Tattach`), not an auth fid read and
  written by a separate agent as in 9P.
