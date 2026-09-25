/* ARP for IPv4 over Ethernet (RFC 826). */
#include "net.h"
#include "cpu.h"
#include "kerrno.h"
#include "kstring.h"
#include "sched.h"
#include "timer.h"

#define ARP_PACKET 28
#define ARP_CACHE 16
#define ARP_TRIES 3
#define ARP_WAIT_TICKS (300 * TIMER_HZ / 1000)
#define OP_REQUEST 1
#define OP_REPLY 2

struct arp_entry {
	struct netif *ifc; /* NULL: unused */
	uint32_t ip;
	uint8_t mac[ETH_ALEN];
	uint64_t last_used;
};

static struct arp_entry cache[ARP_CACHE];
static struct waitq learned = WAITQ_INIT;
static const uint8_t BROADCAST[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static struct arp_entry *lookup(struct netif *ifc, uint32_t ip)
{
	for (int i = 0; i < ARP_CACHE; i++) {
		if (cache[i].ifc == ifc && cache[i].ip == ip) {
			return &cache[i];
		}
	}
	return 0;
}

void arp_learn(struct netif *ifc, uint32_t ip, const uint8_t mac[ETH_ALEN])
{
	uint32_t flags = cpu_irq_save();
	struct arp_entry *e = lookup(ifc, ip);
	if (!e) { /* an unused slot, else the least recently used */
		e = &cache[0];
		for (int i = 0; i < ARP_CACHE && e->ifc; i++) {
			if (!cache[i].ifc || cache[i].last_used < e->last_used) {
				e = &cache[i];
			}
		}
	}
	e->ifc = ifc;
	e->ip = ip;
	memcpy(e->mac, mac, ETH_ALEN);
	e->last_used = timer_ticks();
	cpu_irq_restore(flags);
	waitq_wake_all(&learned);
}

size_t arp_snapshot(uint32_t *ips, uint8_t (*macs)[ETH_ALEN], size_t max)
{
	size_t n = 0;
	uint32_t flags = cpu_irq_save();
	for (int i = 0; i < ARP_CACHE && n < max; i++) {
		if (cache[i].ifc) {
			ips[n] = cache[i].ip;
			memcpy(macs[n], cache[i].mac, ETH_ALEN);
			n++;
		}
	}
	cpu_irq_restore(flags);
	return n;
}

static int send_arp(struct netif *ifc, uint16_t op, const uint8_t dst_mac[ETH_ALEN],
                    uint32_t target_ip, const uint8_t target_mac[ETH_ALEN])
{
	uint8_t f[ETH_HEADER + ARP_PACKET];
	memcpy(f, dst_mac, ETH_ALEN);
	memcpy(f + 6, ifc->mac, ETH_ALEN);
	put_be16(f + 12, 0x0806);
	uint8_t *a = f + ETH_HEADER;
	put_be16(a, 1);          /* Ethernet */
	put_be16(a + 2, 0x0800); /* IPv4 */
	a[4] = ETH_ALEN;
	a[5] = 4;
	put_be16(a + 6, op);
	memcpy(a + 8, ifc->mac, ETH_ALEN);
	put_be32(a + 14, ifc->ip);
	memcpy(a + 18, target_mac, ETH_ALEN);
	put_be32(a + 24, target_ip);
	int rc = ifc->transmit(ifc, f, sizeof(f));
	if (rc == 0) {
		ifc->tx_frames++;
	} else {
		ifc->tx_errors++;
	}
	return rc;
}

void arp_input(struct netif *ifc, const uint8_t *frame, size_t len)
{
	const uint8_t *a = frame + ETH_HEADER;
	if (len < ETH_HEADER + ARP_PACKET || get_be16(a) != 1 || get_be16(a + 2) != 0x0800
	    || a[4] != ETH_ALEN || a[5] != 4 || !ifc->ip) {
		ifc->rx_dropped++;
		return;
	}
	uint16_t op = get_be16(a + 6);
	uint32_t sender_ip = get_be32(a + 14), target_ip = get_be32(a + 24);
	uint8_t sender_mac[ETH_ALEN];
	memcpy(sender_mac, a + 8, ETH_ALEN);

	uint32_t flags = cpu_irq_save();
	bool known = lookup(ifc, sender_ip) != 0;
	cpu_irq_restore(flags);
	/* RFC 826: refresh what we know; add the sender if it asked us. */
	if (sender_ip && (known || target_ip == ifc->ip)) {
		arp_learn(ifc, sender_ip, sender_mac);
	}
	if (op == OP_REQUEST && target_ip == ifc->ip) {
		send_arp(ifc, OP_REPLY, sender_mac, sender_ip, sender_mac);
	}
}

int arp_resolve(struct netif *ifc, uint32_t ip, uint8_t mac[ETH_ALEN])
{
	static const uint8_t unknown[ETH_ALEN];
	for (int attempt = 0; attempt < ARP_TRIES; attempt++) {
		uint32_t flags = cpu_irq_save();
		struct arp_entry *e = lookup(ifc, ip);
		if (e) {
			memcpy(mac, e->mac, ETH_ALEN);
			e->last_used = timer_ticks();
			cpu_irq_restore(flags);
			return 0;
		}
		cpu_irq_restore(flags);

		send_arp(ifc, OP_REQUEST, BROADCAST, ip, unknown);
		if (net_is_network_thread()) {
			return -EHOSTUNREACH; /* it would wait for its own work */
		}
		uint64_t deadline = timer_ticks() + ARP_WAIT_TICKS;
		flags = cpu_irq_save();
		while (!lookup(ifc, ip) && waitq_sleep_until(&learned, deadline)) {
		}
		cpu_irq_restore(flags);
	}
	uint32_t flags = cpu_irq_save();
	struct arp_entry *e = lookup(ifc, ip);
	if (e) {
		memcpy(mac, e->mac, ETH_ALEN);
	}
	cpu_irq_restore(flags);
	return e ? 0 : -EHOSTUNREACH;
}
