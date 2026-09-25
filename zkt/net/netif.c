/* Interfaces, the network thread, loopback, and stack start-up. */
#include "net.h"
#include "cmdline.h"
#include "cpu.h"
#include "heap.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "ne2000.h"
#include "panic.h"
#include "sched.h"

#define ETHERTYPE_IP  0x0800
#define ETHERTYPE_ARP 0x0806
#define LOOPBACK_QUEUE_MAX 32

struct net_stats net_stats;

static struct netif *interfaces;
static struct thread *network_thread;
static struct waitq work = WAITQ_INIT;
static volatile bool work_pending;

struct queued_packet {
	struct queued_packet *next;
	size_t len;
	uint8_t data[];
};
static struct queued_packet *loop_head, *loop_tail;
static size_t loop_count;
static struct netif loopback = { .name = "lo", .loopback = true };

uint32_t inet_sum(uint32_t sum, const void *data, size_t len)
{
	const uint8_t *p = data;
	for (; len > 1; p += 2, len -= 2) {
		sum += (uint32_t)(p[0] << 8 | p[1]);
	}
	if (len) {
		sum += (uint32_t)p[0] << 8;
	}
	return sum;
}

uint16_t inet_fold(uint32_t sum)
{
	while (sum >> 16) {
		sum = (sum & 0xFFFF) + (sum >> 16);
	}
	return (uint16_t)~sum;
}

bool ip_parse(const char *s, uint32_t *ip, int *prefix)
{
	uint32_t v = 0;
	for (int part = 0; part < 4; part++) {
		uint32_t octet = 0;
		int digits = 0;
		for (; *s >= '0' && *s <= '9' && digits < 4; s++, digits++) {
			octet = octet * 10 + (uint32_t)(*s - '0');
		}
		if (!digits || octet > 255 || (part < 3 && *s++ != '.')) {
			return false;
		}
		v = v << 8 | octet;
	}
	int bits = 32;
	if (*s == '/' && prefix) {
		bits = 0;
		int digits = 0;
		for (s++; *s >= '0' && *s <= '9' && digits < 3; s++, digits++) {
			bits = bits * 10 + (*s - '0');
		}
		if (!digits || bits > 32) {
			return false;
		}
	}
	if (*s) {
		return false;
	}
	*ip = v;
	if (prefix) {
		*prefix = bits;
	}
	return true;
}

char *ip_format(uint32_t ip, char *buf)
{
	ksnprintf(buf, 16, "%lu.%lu.%lu.%lu", (unsigned long)(ip >> 24), (unsigned long)(ip >> 16 & 255),
	          (unsigned long)(ip >> 8 & 255), (unsigned long)(ip & 255));
	return buf;
}

void netif_register(struct netif *ifc)
{
	uint32_t flags = cpu_irq_save();
	struct netif **link = &interfaces;
	while (*link) {
		link = &(*link)->next;
	}
	ifc->next = 0;
	*link = ifc;
	cpu_irq_restore(flags);
}

struct netif *netif_first(void)
{
	return interfaces;
}

void netif_notify(void)
{
	work_pending = true;
	waitq_wake_one(&work);
}

bool net_is_network_thread(void)
{
	return network_thread && thread_current() == network_thread;
}

void netif_input(struct netif *ifc, const uint8_t *frame, size_t len)
{
	ifc->rx_frames++;
	if (len < ETH_HEADER) {
		ifc->rx_dropped++;
		return;
	}
	uint16_t type = get_be16(frame + 12);
	if (type == ETHERTYPE_ARP) {
		arp_input(ifc, frame, len);
	} else if (type == ETHERTYPE_IP) {
		/* Learn the sender's MAC from its packet, so replies to it
		 * need no ARP round trip (and the network thread never waits
		 * for one). Only for on-link senders. */
		if (len >= ETH_HEADER + IP_HEADER && ifc->ip) {
			uint32_t src = get_be32(frame + ETH_HEADER + 12);
			if (src && ((src ^ ifc->ip) & ifc->netmask) == 0) {
				arp_learn(ifc, src, frame + 6);
			}
		}
		ip_input(ifc, frame + ETH_HEADER, len - ETH_HEADER);
	} else {
		ifc->rx_dropped++;
	}
}

int loopback_queue(const uint8_t *packet, size_t len)
{
	struct queued_packet *q = kmalloc(sizeof(*q) + len);
	if (!q) {
		return -ENOMEM;
	}
	q->next = 0;
	q->len = len;
	memcpy(q->data, packet, len);
	uint32_t flags = cpu_irq_save();
	if (loop_count == LOOPBACK_QUEUE_MAX) {
		cpu_irq_restore(flags);
		kfree(q);
		loopback.tx_errors++;
		return -ENOMEM;
	}
	if (loop_tail) {
		loop_tail->next = q;
	} else {
		loop_head = q;
	}
	loop_tail = q;
	loop_count++;
	cpu_irq_restore(flags);
	loopback.tx_frames++;
	netif_notify();
	return 0;
}

/* Everything received is processed here, one packet at a time. */
static void network_main(void *unused)
{
	(void)unused;
	for (;;) {
		uint32_t flags = cpu_irq_save();
		while (!work_pending) {
			waitq_sleep(&work);
		}
		work_pending = false;
		cpu_irq_restore(flags);

		for (struct netif *ifc = interfaces; ifc; ifc = ifc->next) {
			if (ifc->poll) {
				ifc->poll(ifc);
			}
		}
		for (;;) {
			flags = cpu_irq_save();
			struct queued_packet *q = loop_head;
			if (q) {
				loop_head = q->next;
				if (!loop_head) {
					loop_tail = 0;
				}
				loop_count--;
			}
			cpu_irq_restore(flags);
			if (!q) {
				break;
			}
			loopback.rx_frames++;
			ip_input(&loopback, q->data, q->len);
			kfree(q);
		}
	}
}

/* ip=A.B.C.D/N and gw=A.B.C.D configure the first Ethernet interface.
 * Without ip=, it gets QEMU's user-networking defaults (10.0.2.15/24,
 * gateway 10.0.2.2); with ip= and no gw=, it has no gateway. */
static void configure(struct netif *ifc)
{
	char value[32], a[16], g[16];
	uint32_t ip = IP_ADDR(10, 0, 2, 15), gw = IP_ADDR(10, 0, 2, 2);
	int prefix = 24;
	if (cmdline_get("ip", value, sizeof(value))) {
		gw = 0;
		if (!ip_parse(value, &ip, &prefix)) {
			kprintf("net: ignoring malformed ip=%s\n", value);
			ip = IP_ADDR(10, 0, 2, 15);
			prefix = 24;
		}
	}
	if (cmdline_get("gw", value, sizeof(value)) && !ip_parse(value, &gw, 0)) {
		kprintf("net: ignoring malformed gw=%s\n", value);
	}
	ifc->ip = ip;
	ifc->netmask = prefix ? ~0u << (32 - prefix) : 0;
	ifc->gateway = gw;
	if (gw) {
		kprintf("%s: %s/%d, gateway %s\n", ifc->name, ip_format(ip, a), prefix, ip_format(gw, g));
	} else {
		kprintf("%s: %s/%d, no gateway\n", ifc->name, ip_format(ip, a), prefix);
	}
}

void net_init(void)
{
	loopback.ip = IP_LOOPBACK;
	loopback.netmask = IP_ADDR(255, 0, 0, 0);
	netif_register(&loopback);

	ne2000_probe();
	for (struct netif *ifc = interfaces; ifc; ifc = ifc->next) {
		if (!ifc->loopback) {
			configure(ifc);
			break;
		}
	}
	network_thread = thread_create("net", network_main, 0);
	if (!network_thread) {
		panic("net: cannot start the network thread");
	}
}
