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
- `zrp_server.c` — serves a directory of a namespace (`export=`)
- `zrp_client.c` — a remote tree as a filesystem (`mount`)
- `net_selftest.c` — a ZRP server and client over loopback at every
  boot

Drivers live in `zkt/drivers/` (`ne2000.c`) and register a `struct
netif`.
