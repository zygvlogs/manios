# M15 — Network Cards for VirtualBox, and DHCP

**Status:** Achieved (2026-09-26). Released as **0.15.0**. Asked for
after ManiOS 0.14 ran in VirtualBox, whose network cards ManiOS didn't
know: until now it drove only the NE2000 (M10), which VirtualBox
doesn't offer.

```
pcn0: AMD PCnet at pci 00:03.0, io 0xc000 irq 11, 52:54:00:12:34:56
pcn0: 10.0.2.15/24, gateway 10.0.2.2
dhcp: pcn0: 10.0.2.15/24, gateway 10.0.2.2 (from 10.0.2.2, for 86400 s)
```

## What M15 delivers

| Piece | Files | Summary |
|---|---|---|
| AMD PCnet | `zkt/drivers/pcnet.c` | PCnet-PCI II / PCnet-FAST III (1022:2000): VirtualBox's default card, QEMU's `pcnet`. Interface `pcn0` |
| Intel 8254x | `zkt/drivers/e1000.c` | 82540EM and relatives: VirtualBox's PRO/1000 cards, QEMU's `e1000` (and its default card). Interface `em0` |
| DHCP client | `zkt/net/dhcp.c` | Without `ip=` (or with `ip=dhcp`): DISCOVER, OFFER, REQUEST, ACK; netmask, gateway, lease; asked again at half the lease |
| Shared IRQs | `zkt/arch/i386/irq.c` | Up to four handlers per line: PCI cards share lines |
| Device registers | `zkt/mm/mmio.c`, `VMM_UNCACHED` | A kernel window for memory-mapped registers, uncached |
| PCI | `pci_enable`, `pci_bar_io`, `pci_bar_mem` | I/O, memory and bus mastering on; BARs by kind |
| Tests | `tests/net_test.py`, `console_test.py` | The whole single-machine network suite over each card; DHCP against an awkward server of the test's own and against QEMU's; tests' machines have no card unless they ask |

## Design decisions

- **Bus masters, not programmed I/O.** Unlike the NE2000, both cards
  move frames themselves: the driver gives them rings of descriptors
  and buffers in memory. Each ring and each pair of 2 KiB buffers is a
  kernel page (`kpage_alloc`) whose physical address the card is given;
  x86 DMA is cache-coherent, so ordering the descriptor's ownership
  bit after its contents (a compiler barrier) is all it takes.
- **PCnet in software style 2**, 32-bit structures: an initialization
  block, then 32 receive and 8 transmit descriptors. The chip may have
  been left by firmware in word or double-word I/O mode (only a
  hardware reset clears it), so the driver resets both ways and checks
  which answers. Its registers sit behind an address port; every
  address/data pair is done with interrupts off, since the IRQ handler
  uses the address port too.
- **e1000 with legacy descriptors**, which every 8254x has: 32 receive
  (rings are multiples of 128 bytes) and 8 transmit. The Ethernet
  address comes from RAL0/RAH0, which the card loads from its EEPROM at
  reset, and from the EEPROM itself (EERD) otherwise. Registers are in
  memory, mapped uncached (PCD|PWT) through the new MMIO window.
- **Interrupts only wake the network thread**, as for the NE2000; the
  thread takes frames off the ring in `poll()` and gives descriptors
  back. Transmit copies the frame into the next slot, padding it to 60
  bytes, and waits (up to 100 ms) if the card hasn't sent what was
  there before.
- **PCI interrupt lines are shared**, so `irq_install_handler` keeps a
  short list per line, and each handler checks its own card's status
  first (CSR0's INTR, e1000's ICR).
- **DHCP after the self-tests.** The client is a thread, and the boot
  self-tests count threads and memory; so the kernel starts it once
  they have passed, waits up to 3 s for a lease, and otherwise goes on
  booting while it keeps asking. Meanwhile the interface has the
  addresses QEMU's and VirtualBox's NAT hand out anyway (10.0.2.15/24
  via 10.0.2.2), as before M15.
- **DHCP messages are built whole** (Ethernet, IP from 0.0.0.0 to
  255.255.255.255, UDP, BOOTP padded to 300 bytes) and sent on the
  interface directly: the IP layer has no business sending from no
  address. Replies are asked for by broadcast; the IP layer passes UDP
  for port 68 whatever its destination, and the client checks the
  transaction ID, the client address and the magic cookie, and walks
  the options with their lengths checked.
- **Names from the BSDs**: `pcn0` and `em0`, as those systems call
  these cards. (Only the names: the drivers are written from the
  datasheets.)
- **Tests choose their cards.** QEMU gives a machine an e1000 on its
  user network unless told otherwise; now that ManiOS drives it, the
  test harness passes `-nic none` unless a test asks for a network, so
  every other test boots as before.

## Verification

- **`net_test.py`**: the single-machine suite -- ARP, ICMP, UDP
  checksums, malformed packets, a flood, the guest's ZRP server against
  the host's client on a lossy link, the guest's client against the
  host's server, the monitor's counters and ping, `Rpending` -- runs
  over the NE2000, the PCnet and the e1000 in turn, all passing. Under
  the flood (400 pings as fast as the host can send them; what gets
  answered depends a lot on the host's timing) the NE2000 answered 54
  to 57, the e1000 121 to 400, and the PCnet 106 to 234 with 16 receive
  descriptors, 215 to 327 once it had 32 like the e1000.
- **One unexplained failure.** In one full `make test` run, one of the
  300 paced pings over the PCnet (16 descriptors then) got no reply.
  It didn't come back in about 50 runs of the same tests since (each
  printing the card's counters: no transmit errors; the drops are the
  floods' missed frames), nor in 2,400 paced pings or 100 single pings
  after floods; reading QEMU's PCnet emulation (receive, descriptor
  polling, transmit, CSR0) found nothing the driver does wrong. The
  receive ring was doubled anyway, which the flood figures above show
  was worth it; if it comes back, the counters will say more.
- Found on the way: the drivers counted each frame they sent, and so
  did the stack (ARP, IP) -- `tx` read twice the truth. Only the stack
  counts now.
- **DHCP against the test's own server**, over a PCnet: it ignores the
  first DISCOVER, sends an OFFER for another transaction and one whose
  last option runs past the end before the real one, and NAKs the first
  REQUEST. The guest retransmits, ignores both bad OFFERs, requests the
  offered address from the right server, starts over after the NAK, and
  gets its lease (10.0.0.77/24 via 10.0.0.1); every message it sends
  comes from 0.0.0.0 and port 68, carries the broadcast flag and its
  own address, and is at least 300 bytes. The boot says it is still
  asking; the lease line comes after the prompt; `/dev/net` shows it,
  and the host answers the guest's pings from its new address.
- **DHCP against QEMU's server** (its user network on 192.168.76.0/24,
  so the lease isn't the default), over the PCnet and the e1000: the
  lease, and QEMU's gateway answering pings.
- By hand: all three cards pinging QEMU's gateway; plain `qemu-system-i386
  -cdrom manios.iso` (QEMU's default e1000) gets its lease.
- `make test`: 241 checks (194 at 0.14.2), none failing.
- **Negative controls**, each caught:

  | Sabotage | Caught by |
  |---|---|
  | The PCnet driver never gives receive descriptors back | Its suite: 1 of 300 paced pings answered, no ZRP, no ping |
  | The e1000 driver never moves the receive tail | Its suite: 16 of 300 answered (the ring, once), no ZRP |
  | DHCP takes replies for any transaction | The host's server: the guest requested the stray OFFER's address |
  | The IP layer drops UDP for port 68 not addressed to us | The host's server: no lease |

## Known limits

- VirtualBox itself is untested so far: the tests use QEMU's PCnet-PCI
  II and 82540EM. The PCnet-FAST III (VirtualBox's default) is
  programmed the same way as the PCnet-PCI II; the 82543GC and 82545EM
  ("T Server", "MT Server") are of the 82540EM's family.
- One card of each kind; DHCP configures the first Ethernet interface
  only.
- No virtio-net, no DNS (ManiOS has no names to look up yet), no TCP.
- DHCP renews by asking afresh at half the lease rather than by the
  RENEWING/REBINDING states; a server that hands out a different
  address then changes the machine's.
