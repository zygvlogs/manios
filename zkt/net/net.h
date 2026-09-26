/* The network stack: interfaces, Ethernet/ARP, IPv4, ICMP, UDP. Design
 * notes: docs/milestones/M10-networking-zrp.md. Addresses and ports are
 * kept in host byte order everywhere except inside packets. */
#ifndef ZKT_NET_NET_H
#define ZKT_NET_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ETH_ALEN 6
#define ETH_HEADER 14
#define ETH_MTU 1500
#define ETH_FRAME_MAX (ETH_HEADER + ETH_MTU)
#define ETH_FRAME_MIN 60 /* without the CRC; shorter frames are padded */
#define IP_HEADER 20
#define UDP_HEADER 8
#define UDP_PAYLOAD_MAX (ETH_MTU - IP_HEADER - UDP_HEADER) /* 1472: no fragmentation */

#define IP_ADDR(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
#define IP_LOOPBACK IP_ADDR(127, 0, 0, 1)

/* Big-endian (network order) loads and stores. Shifts, not bswap: that
 * instruction arrived with the 486. */
static inline uint16_t get_be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline uint32_t get_be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static inline void put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void put_be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

/* The Internet checksum (RFC 1071): continue a sum over more data, then
 * fold it. A packet whose stored checksum is right sums to 0. */
uint32_t inet_sum(uint32_t sum, const void *data, size_t len);
uint16_t inet_fold(uint32_t sum);

/* A network interface. A driver fills in name, mac and the operations,
 * then calls netif_register(); the stack owns the rest. */
struct netif {
	char name[8];
	uint8_t mac[ETH_ALEN];
	bool loopback;
	/* Sends one Ethernet frame (header included, not yet padded). May
	 * block; called from threads only. */
	int (*transmit)(struct netif *ifc, const uint8_t *frame, size_t len);
	/* Hands every frame received since the last call to
	 * netif_input(). Runs on the network thread. */
	void (*poll)(struct netif *ifc);
	void *driver;

	uint32_t ip, netmask, gateway; /* 0: not configured */
	uint32_t rx_frames, tx_frames, rx_dropped, tx_errors;
	struct netif *next;
};

void netif_register(struct netif *ifc);
struct netif *netif_first(void);
/* For drivers: frames arrived (call from the IRQ handler); the network
 * thread will poll. */
void netif_notify(void);
/* For poll(): one received frame. The stack copies what it keeps. */
void netif_input(struct netif *ifc, const uint8_t *frame, size_t len);

/* Starts the stack: loopback interface (127.0.0.1), NIC drivers, the
 * network thread, and addresses from the command line (ip=A.B.C.D/N,
 * gw=A.B.C.D). */
void net_init(void);

/* DHCP (dhcp.c). Without ip= on the command line, or with ip=dhcp, the
 * first Ethernet interface starts with 10.0.2.15/24 via 10.0.2.2 (the
 * addresses of QEMU's and VirtualBox's NAT) and asks a DHCP server for
 * its own; net_start_dhcp() starts asking -- after the boot self-tests,
 * which count threads and memory -- and waits up to wait_ms for the
 * first answer. The asking goes on in the background. */
#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
void net_start_dhcp(uint32_t wait_ms);
void dhcp_start(struct netif *ifc);
bool dhcp_leased(void);

/* "A.B.C.D" (with optional "/N") to an address; false if malformed. */
bool ip_parse(const char *s, uint32_t *ip, int *prefix);
/* Formats into buf (at least 16 bytes); returns buf. */
char *ip_format(uint32_t ip, char *buf);

/* Internal to the stack. */
void arp_input(struct netif *ifc, const uint8_t *frame, size_t len);
/* The MAC for next hop `ip` on ifc: cached, or asked for with ARP and
 * waited for (up to about a second). Never waits on the network thread. */
int arp_resolve(struct netif *ifc, uint32_t ip, uint8_t mac[ETH_ALEN]);
void arp_learn(struct netif *ifc, uint32_t ip, const uint8_t mac[ETH_ALEN]);
size_t arp_snapshot(uint32_t *ips, uint8_t (*macs)[ETH_ALEN], size_t max);

void ip_input(struct netif *ifc, const uint8_t *packet, size_t len);
/* Sends an IP packet carrying `payload` (routing, ARP, loopback). */
int ip_output(uint32_t dst, uint8_t protocol, const uint8_t *payload, size_t len);
/* The source address a packet to dst would carry. */
uint32_t ip_source_for(uint32_t dst);
bool net_is_network_thread(void);
/* Queues a packet for the network thread to deliver as received on the
 * loopback interface. */
int loopback_queue(const uint8_t *packet, size_t len);

void icmp_input(uint32_t src, uint32_t dst, const uint8_t *data, size_t len);
/* Sends an ICMP echo request and waits for the reply: the round trip
 * in ms, or -ETIMEDOUT and the like. */
long icmp_ping(uint32_t dst, uint16_t seq, uint32_t timeout_ms);

void udp_input(uint32_t src, uint32_t dst, const uint8_t *data, size_t len);

/* Statistics the tests and the monitor look at. */
struct net_stats {
	uint32_t ip_in, ip_bad, ip_fragments, ip_not_ours;
	uint32_t udp_in, udp_bad, udp_no_port, icmp_echo_replied;
};
extern struct net_stats net_stats;

#endif
