# ZRP — the ZygKernel Resource Protocol, version `ZRP1`

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

Two independent implementations follow this document: the kernel's
(`zkt/net/zrp_*.c`) and the test suite's (`tests/net_test.py`). Each is
tested against the other.

## Transport

Version 1 runs over **UDP, port 5640**, one message per datagram. A
message is at most **1472 bytes**, the largest UDP payload a single
Ethernet frame carries, so IP never fragments. A client is identified
by its IP address and UDP port; that pair is a *session*.

**Reliability.** UDP may lose, duplicate or reorder datagrams, so ZRP
adds its own reliability, in the style of Plan 9's IL:

- A client has **one request outstanding** per session.
- It **retransmits** a request byte for byte, with the same tag, until
  a reply with that tag arrives. The kernel waits 150 ms, then doubles
  the wait, for six tries (about 9.5 s in all), then reports
  `ETIMEDOUT`.
- A client **ignores replies whose tag isn't the one it is waiting
  for**. These are late copies of earlier replies.
- A server **keeps each session's last request and its reply**. If the
  identical request arrives again, the server resends the saved reply
  without performing the request a second time. A repeated `Twalk` or
  `Tclunk` must not fail just because the first copy already succeeded.
- Tags count up, wrapping before 0xFFFF. 0xFFFF (`NOTAG`) is reserved
  for `Tversion`.

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
| — | | 64 `Rerror`: `errno[2]` |

- **`Tversion`** starts, or restarts, a session and drops all its fids.
  The version string must be `ZRP1`. The server replies with the
  smaller of its own `msize` and the client's, and neither side may
  then send a longer message. The minimum `msize` is 256. Every other
  request before `Tversion` gets `EPROTO`.
- **`Tattach`** makes `fid` refer to the root of the export named
  `aname`. The empty name is the default export, and version 1 servers
  have only that one.
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
  allows 32: `EMFILE`).
- **`Rerror`** carries a ZKT error number (`zkt_abi.h`), which the
  client returns as the result of the failed operation. For example:
  - `ENOENT` 2: no such name;
  - `EBADF` 9: unknown fid;
  - `EINVAL` 22: bad name, or fid in use;
  - `EROFS` 30: not writable;
  - `EPROTO` 71: malformed request, unknown type, or no session.

Every field a server receives is validated before use, and a client
checks every reply the same way. A reply whose `count` exceeds its data,
or a stat with an unknown type, makes the operation fail with `EPROTO`;
it never crashes or corrupts the client.

## Security

Version 1 has **no authentication and no encryption**. Anyone who can
send UDP to port 5640 can read everything the server exports, and write
whatever the exported tree allows. Export only on networks you trust,
as with NFS before Kerberos or early Plan 9 file servers. Adding
authentication is future work, planned with the cluster roles (M13).

## Differences from 9P

- **No open.** Reads and writes work directly on walked fids; the
  server opens files as it needs to.
- **One name per `Twalk`.** The reply is a stat, not a list of qids,
  so the client learns the type and size in the same round trip.
- **No qids.** Mount points are keyed by path; see ADR-0003.
- **Directory reads count entries, not bytes.**
- **Errors are numbers, not strings.**
- **Reliability over UDP is part of the protocol** (above), where 9P
  assumes a reliable transport.
