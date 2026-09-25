# M10 — Networking, and ZRP over the Network

**Status:** Achieved (2026-09-25). Implements
[FOUNDING-PROPOSAL.md §6](../FOUNDING-PROPOSAL.md#6-development-roadmap)
milestone M10, and the network half of
[ADR-0003](../adr/0003-plan9-namespaces-and-resource-protocol.md). The
protocol is specified in [docs/zrp.md](../zrp.md).

One ManiOS machine can now use another's files as if they were its own:

```
server$ qemu ... -append "ip=10.0.0.1/24 export=/n/ata0p1"
terminal% mount udp!10.0.0.1 /n
terminal% sum /n/frag.bin
/n/frag.bin: 20000 bytes, fnv1a 0f8a3c24
terminal% /n/bin/fault null        # a program stored on the server's disk
```

## What M10 delivers

| Piece | Files | Summary |
|---|---|---|
| NE2000 driver | `zkt/drivers/ne2000.c` | ISA DP8390 card (QEMU `ne2k_isa`), programmed I/O, interrupt-driven |
| Interfaces | `zkt/net/netif.c` | Interface list, the network thread, loopback, `ip=`/`gw=` configuration |
| ARP | `zkt/net/arp.c` | 16-entry cache, requests and replies, learning from received packets |
| IPv4, ICMP | `zkt/net/ip.c` | Routing (on-link or gateway), header checks, echo reply, `ping` |
| UDP | `zkt/net/udp.c` | Kernel endpoints with bounded queues and timed receive |
| ZRP | `zkt/net/zrp*.c`, `docs/zrp.md` | Protocol codec, server (thread per export), client (a VFS filesystem) |
| `mount` | `syscall.c`, `userland/bin/mount.c` | `SYS_MOUNT`: attach to a server and bind its tree into the namespace |
| Command line | `zkt/kernel/cmdline.c` | `ip=`, `gw=`, `export=`, from the boot loader |
| Timed waits | `zkt/scheduler/sched.c` | `waitq_sleep_until`, for timeouts and retransmission |
| Monitor | `monitor.c` | `net`, `arp`, `ping`, `mount`, `export` |
| Self-test | `zkt/net/net_selftest.c` | A ZRP server and client over loopback at every boot |
| Wire tests | `tests/net_test.py` | The test is the other end of the cable, and connects two ManiOS machines |

## Design decisions

- **An NE2000 first.** It is the ISA card of the 386 era, needs no PCI,
  and QEMU emulates it faithfully. The driver was written from the
  DP8390's register description. It uses the card's remote DMA in
  16-bit mode, with a 6-page transmit buffer followed by the receive
  ring in the card's 16 KiB of memory. The IRQ handler only
  acknowledges events and wakes threads. Receiving happens on the
  network thread and sending on the caller's thread, serialised by a
  mutex because both use the one remote DMA channel.
- **One network thread** processes every received packet in order: ARP,
  IP, ICMP replies, and delivery to UDP endpoints. It never waits for
  anything the network would have to deliver. `arp_resolve` refuses to
  wait when called on the network thread, and senders' MACs are learned
  from their packets, so a reply goes out without an ARP round trip.
  Loopback, `127.0.0.1` or the machine's own address, is a queue this
  thread drains, so even a local ZRP mount goes through the whole
  stack.
- **No IP fragmentation, in either direction.** Fragments are counted
  and dropped. ZRP never needs fragmentation, because its messages fit
  one Ethernet frame (1472 bytes).
- **UDP with Plan 9 IL-style reliability, not TCP.** Plan 9 carried 9P
  over IL, a small reliable-datagram protocol, rather than TCP, for the
  same reason: a request/response protocol with one outstanding request
  needs only
  - retransmission, with the same tag;
  - a reply cache on the server, so repeated requests aren't performed
    twice;
  - and ignoring stale replies.

  That is a few dozen lines, against the thousands a correct TCP needs.
  A TCP transport can come later: ZRP's message format doesn't depend
  on UDP.
- **The ZRP client is a filesystem.** Each vnode is a fid on the
  server. A walk costs one round trip, and returns the stat, so type
  and size arrive with it. Directories are read a message-full of
  entries at a time and cached. Releasing a vnode clunks its fid. A
  remote tree is bound into a namespace like any other, and unions with
  local directories work: `bind -a /n/bin /bin` makes the server's
  programs runnable.
- **The server serves a namespace, not a disk.** It resolves paths in
  the namespace of whoever started it, so it exports that view of the
  system, binds included, as Plan 9's `exportfs` does. Walks are checked
  so they can never climb out of the export (`..`, `/` in names).
- **`mount` is a system call:** `mount(dial, old, flag, aname)`.
  - The dial string follows Plan 9's style: `udp!10.0.0.1!5640`.
  - The call attaches, then binds exactly as `bind` does, with the same
    replace, before and after flags.
  - A child shares its parent's namespace, so `mount` works as a
    program run from the shell. It needs no builtin.
- **Found while building:**
  - **The namespace held its lock across walks.** Harmless while every
    filesystem was local and fast. With ZRP, one unreachable server
    would have frozen every thread sharing the namespace for the whole
    retransmission window. The loopback self-test deadlocked on it,
    because its server shares the test's namespace. Now the lock is held
    only while the mount table is read. The vnodes of a walk in progress
    are kept alive by references, and replaced mounts are released
    outside the lock (releasing a remote vnode is a network request).
  - **Waits needed timeouts.** The scheduler gained
    `waitq_sleep_until`: a thread waits on a queue and a deadline at
    once. A second link field lets it sit on the sleep list at the same
    time; whichever fires first removes it from the other.
- **Configuration from the kernel command line:**
  - `ip=A.B.C.D/N` and `gw=A.B.C.D` set the interface's address;
  - `export=PATH` starts a ZRP server;
  - without `ip=`, the interface takes QEMU's user-network defaults
    (10.0.2.15/24, gateway 10.0.2.2).

  `make run` now attaches an NE2000 on QEMU's user network, and passes
  extra QEMU arguments through, e.g.
  `tools/qemu-run.sh build/manios-zkt.elf -append "export=/boot"`.

## Verification

- **At every boot, and in `make test`:**
  - **scheduler**: a timed wait woken early, and one that times out on
    schedule, must leave both lists clean;
  - **network**: `net_selftest` pings `127.0.0.1`, starts a ZRP server
    for `/boot`, and mounts it over loopback in a private namespace. It
    then compares a small file, a multi-message file (`/bin/sh`), and a
    directory listing with the local originals, and checks:
    - `ENOENT` for a missing name, and for an unknown export name;
    - `EROFS` for a write;
    - a program loaded and run over ZRP;
    - no fids left after unmounting, no leaked heap, and the server
      stopping cleanly.

  The M10 marker reports this self-test.
- **`tests/net_test.py`** (part of `make test`, 27 checks). The test
  process is the guest's network peer: QEMU's socket netdev delivers the
  NE2000's frames to it over TCP. The test implements ARP, IPv4, ICMP,
  UDP and ZRP itself, and checks every checksum the guest sends. It
  covers:
  - **Link**: an ARP reply for the guest's address, and an ICMP echo
    reply with the same data and valid checksums.
  - **Hostile input**, each dropped without a reply and counted in the
    guest's `net` statistics: a bad IP checksum, a truncated header, a
    total length beyond the frame, both kinds of fragment, a bad ICMP
    checksum, a header length that lies, a packet for another address,
    a bad UDP checksum, a runt UDP header, IPv6, a runt ARP, and a runt
    frame. The guest still answers afterwards.
  - **Load**: 300 pings of every size from 1 to 200 bytes (odd lengths
    exercise the 16-bit transfers), paced so the receive ring wraps
    many times; all are answered. Then a 400-ping flood the ring can't
    hold: the card drops what doesn't fit, as real hardware does, and
    the guest keeps answering.
  - **The guest's ZRP server**, driven by the test's own client:
    - version, attach and walk; reads at offsets; `Tstat`;
    - a directory read by entry index, a message at a time, matching
      the build's `bin/`;
    - `/bin/sh` read whole and compared byte for byte with the build's
      copy;
    - every error (`ENOENT`, `EINVAL` for `..`, `/` and a fid in use,
      `EBADF`, an unknown export, `EROFS`, `EPROTO` for an unknown type
      and before `Tversion`);
    - no reply to garbage;
    - a repeated `Twalk` answered from the reply cache;
    - clunk, then `EBADF`;
    - `msize` negotiation bounding reads.
  - **The guest's ZRP client, over a lossy link:** the shell mounts the
    test's server, which drops every 7th request, drops every 11th
    reply, and duplicates every 5th. The guest must:
    - list directories, including one of 60 entries that takes several
      reads;
    - `cat` and `cd` with relative paths;
    - produce `sum`s of a 40 KB file that match;
    - fail cleanly for a missing file, for replies that lie about their
      length or carry an unknown file type (`protocol error`), for an
      unreachable host (`host unreachable`), for a port with no server
      (`timed out`), and for a malformed dial string.

    Every dropped request must come back byte for byte, and every
    dropped reply must be recovered.
  - **Two ManiOS machines**, connected directly. One exports a FAT
    volume, and the other mounts it and:
    - lists it;
    - checksums a fragmented file against the host's own reading of
      the disk image;
    - reads a file typed over the PS/2 keyboard;
    - runs a program stored on the server (killed by its page fault,
      reported by the terminal's shell).

    Then, from the terminal's monitor, `run` of the same program exits
    with 42, and `ping` gets its replies.
- **Negative controls** (temporary sabotage, reverted afterward):

  | Sabotage | Caught by |
  |---|---|
  | Short frames padded by a second, odd-address transfer (the original code) | Guest server and client tests: the guest sent UDP with bad checksums |
  | IP header checksum not verified | *the guest answered a malformed packet*; counters |
  | Fragments accepted | Same |
  | UDP checksum not verified | The `net` counters check |
  | Server accepting `..` in a walk | *walking ..: expected Rerror 22* |
  | Server without its reply cache | *a retransmitted Twalk was not idempotent* |
  | Client accepting a reply with any tag | Wrong file contents over the lossy link. The first version of the lossy link never duplicated a reply, so it missed this sabotage; duplicated and dropped replies were added |
  | Namespace lock held around each walk | The loopback self-test's 10 s deadline |
  | Timed wait woken early left on the sleep list | Boot hangs after M4 (no M5 marker) |
  | NE2000 IRQ acknowledged once instead of until clear | **Not caught.** QEMU never raises a card event between two instructions of the handler, so the race can't occur in emulation. The loop follows the hardware's documented behaviour and is kept |
- **Found by the wire tests before any sabotage:** the odd-address
  padding bug above. Loopback never touches the card, so the self-test
  could not see it. The test implementation also had its own bugs. A
  ping responder was missing from the test's peer. Two mistakes were in
  the test's expectations: it mounted onto a path that doesn't exist on
  the terminal (mount points must exist, as in Plan 9), and it put a
  malformed entry where it broke a listing the test expected to
  succeed.
- All earlier tests pass: `make test` runs 127 checks. An 8 MiB machine
  with the NIC boots and exports; the kernel boots under GRUB.

## Known limits

- **No security in ZRP v1**: no authentication, no encryption (see
  [docs/zrp.md](../zrp.md#security)).
- One NIC model (NE2000 at 0x300, IRQ 9). No DHCP, and no DNS: dial
  strings take numeric addresses.
- No TCP, no IP fragmentation, no multicast, and no user-level socket
  API. Programs reach the network through `mount`.
- One outstanding request per ZRP session, so throughput is one round
  trip per 1.4 KB.
- One export per server (the default `aname`), and one server on port
  5640 (the monitor's `export`, or `export=`).
- The DP8390's receive-overflow recovery procedure (for real cards
  under sustained overload) is not implemented. QEMU's card simply
  drops frames when the ring is full, which the flood test covers.
- Successful `Twrite` isn't exercised: ManiOS has no writable
  filesystem yet, so the tests only cover its error path.
