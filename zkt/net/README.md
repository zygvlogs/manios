# zkt/net/

The network stack and ZRP. Design and verification:
[M10 notes](../../docs/milestones/M10-networking-zrp.md); the protocol:
[docs/zrp.md](../../docs/zrp.md).

- `netif.c` — interfaces, the network thread that processes every
  received packet, loopback, the Internet checksum, and configuration
  from the kernel command line (`ip=`, `gw=`)
- `arp.c` — ARP cache, requests and replies
- `ip.c` — IPv4 (no fragments) and ICMP echo (`icmp_ping`)
- `udp.c` / `udp.h` — UDP endpoints for kernel code
- `zrp.h`, `zrp_msg.c` — ZRP messages
- `zrp_server.c` — serves named exports, each a directory of some
  namespace (`export=` at boot; `SYS_EXPORT` from a process), from
  worker threads, with `Rpending` for requests that wait; device `zrp`
  ([M13 notes](../../docs/milestones/M13-cluster-roles.md))
- `zrp_auth.c` — the cluster key (`key=`), nonces, and the proofs
  exchanged in `Rauth` and `Tattach`
- `zrp_client.c` — a remote tree as a filesystem: over UDP (`mount`),
  or over a pipe to a server that is a program (`mountfd`, M12), with
  many requests in flight
- `net_selftest.c` — a ZRP server and client over loopback at every
  boot: files, programs, named exports, authentication
- `netif.c` also registers device `net` (each interface's address)

Drivers live in `zkt/drivers/` (`ne2000.c`) and register a `struct
netif`.
