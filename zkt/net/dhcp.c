/* A DHCP client (RFC 2131, options from RFC 2132) for one interface:
 * DISCOVER, OFFER, REQUEST, ACK, then the address, netmask and gateway
 * it was given; asked again at half the lease. VirtualBox's and QEMU's
 * NAT answer it, and so does a home router on a bridged network.
 *
 * Messages go out as whole Ethernet frames built here -- from 0.0.0.0
 * to the broadcast address, which the IP layer doesn't send -- asking
 * for broadcast replies; replies come in on UDP port 68, which the IP
 * layer passes whatever address they carry. One thread does it all. */
#include <stdbool.h>
#include "cpu.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "net.h"
#include "sched.h"
#include "timer.h"
#include "udp.h"

#define BOOTP_REQUEST 1
#define BOOTP_REPLY 2
#define BOOTP_SIZE 300 /* with options padded: what old servers expect */
#define BOOTP_OPTIONS 240
#define MAGIC 0x63825363u

#define OPT_PAD 0
#define OPT_NETMASK 1
#define OPT_ROUTER 3
#define OPT_REQUESTED_IP 50
#define OPT_LEASE_TIME 51
#define OPT_MESSAGE_TYPE 53
#define OPT_SERVER_ID 54
#define OPT_PARAMETERS 55
#define OPT_END 255

#define DISCOVER 1
#define OFFER 2
#define REQUEST 3
#define ACK 5
#define NAK 6

struct lease {
	uint32_t ip, netmask, gateway, server, seconds;
};

static struct netif *ifc;
static volatile bool leased;
static uint32_t xid;
static uint16_t ip_id;

bool dhcp_leased(void)
{
	return leased;
}

/* Sends a DISCOVER, or a REQUEST for `want` from `server`. */
static int send_message(int type, uint32_t want, uint32_t server)
{
	static uint8_t frame[ETH_HEADER + IP_HEADER + UDP_HEADER + BOOTP_SIZE];
	memset(frame, 0, sizeof(frame));
	uint8_t *eth = frame, *ip = eth + ETH_HEADER, *udp = ip + IP_HEADER, *b = udp + UDP_HEADER;

	memset(eth, 0xFF, ETH_ALEN);
	memcpy(eth + 6, ifc->mac, ETH_ALEN);
	put_be16(eth + 12, 0x0800);

	b[0] = BOOTP_REQUEST;
	b[1] = 1; /* Ethernet */
	b[2] = ETH_ALEN;
	put_be32(b + 4, xid);
	put_be16(b + 10, 0x8000); /* reply by broadcast: we have no address yet */
	memcpy(b + 28, ifc->mac, ETH_ALEN);
	put_be32(b + 236, MAGIC);
	uint8_t *o = b + BOOTP_OPTIONS;
	*o++ = OPT_MESSAGE_TYPE;
	*o++ = 1;
	*o++ = (uint8_t)type;
	if (type == REQUEST) {
		*o++ = OPT_REQUESTED_IP;
		*o++ = 4;
		put_be32(o, want);
		o += 4;
		*o++ = OPT_SERVER_ID;
		*o++ = 4;
		put_be32(o, server);
		o += 4;
	}
	*o++ = OPT_PARAMETERS;
	*o++ = 3;
	*o++ = OPT_NETMASK;
	*o++ = OPT_ROUTER;
	*o++ = OPT_LEASE_TIME;
	*o = OPT_END;

	put_be16(udp, DHCP_CLIENT_PORT);
	put_be16(udp + 2, DHCP_SERVER_PORT);
	put_be16(udp + 4, UDP_HEADER + BOOTP_SIZE);
	/* checksum 0: none, which IPv4 allows */

	ip[0] = 0x45;
	put_be16(ip + 2, IP_HEADER + UDP_HEADER + BOOTP_SIZE);
	put_be16(ip + 4, ip_id++);
	ip[8] = 64;
	ip[9] = 17;
	put_be32(ip + 16, 0xFFFFFFFFu); /* from 0.0.0.0 */
	put_be16(ip + 10, inet_fold(inet_sum(0, ip, IP_HEADER)));
	return ifc->transmit(ifc, frame, sizeof(frame));
}

/* A reply to our current transaction: its message type (0 if it isn't
 * one), and the lease it describes. Options are walked with their
 * lengths checked; anything malformed is not a reply. */
static int parse(const uint8_t *b, long len, struct lease *l)
{
	if (len < BOOTP_OPTIONS + 3 || b[0] != BOOTP_REPLY || get_be32(b + 4) != xid
	    || memcmp(b + 28, ifc->mac, ETH_ALEN) || get_be32(b + 236) != MAGIC) {
		return 0;
	}
	memset(l, 0, sizeof(*l));
	l->ip = get_be32(b + 16);
	int got_type = 0;
	for (long i = BOOTP_OPTIONS; i < len && b[i] != OPT_END;) {
		if (b[i] == OPT_PAD) {
			i++;
			continue;
		}
		if (i + 2 > len || i + 2 + b[i + 1] > len) {
			return 0;
		}
		uint8_t code = b[i], n = b[i + 1];
		const uint8_t *v = b + i + 2;
		if (code == OPT_MESSAGE_TYPE && n == 1) {
			got_type = v[0];
		} else if (code == OPT_NETMASK && n == 4) {
			l->netmask = get_be32(v);
		} else if (code == OPT_ROUTER && n >= 4) {
			l->gateway = get_be32(v);
		} else if (code == OPT_SERVER_ID && n == 4) {
			l->server = get_be32(v);
		} else if (code == OPT_LEASE_TIME && n == 4) {
			l->seconds = get_be32(v);
		}
		i += 2 + n;
	}
	return got_type;
}

/* Waits up to ms for a reply of `type` (or, waiting for an ACK, a NAK)
 * to the current transaction: its type, or 0. */
static int await(struct udp_endpoint *ep, int type, uint32_t ms, struct lease *l)
{
	static uint8_t buf[576];
	uint64_t until = timer_uptime_ms() + ms;
	for (;;) {
		uint64_t now = timer_uptime_ms();
		if (now >= until) {
			return 0;
		}
		uint32_t src;
		uint16_t port;
		long n = udp_recv(ep, buf, sizeof(buf), &src, &port, (uint32_t)(until - now));
		int got = n > 0 && port == DHCP_SERVER_PORT ? parse(buf, n, l) : 0;
		if (got == type || (type == ACK && got == NAK)) {
			return got;
		}
	}
}

/* One exchange, DISCOVER to ACK, each step tried three times. */
static bool acquire(struct udp_endpoint *ep, struct lease *l)
{
	xid = (uint32_t)timer_uptime_ms() * 2654435761u ^ get_be32(ifc->mac + 2);
	struct lease offer;
	bool offered = false;
	for (int i = 0; i < 3 && !offered; i++) {
		send_message(DISCOVER, 0, 0);
		offered = await(ep, OFFER, 2000u << i, &offer) == OFFER;
	}
	if (!offered || !offer.ip) {
		return false;
	}
	for (int i = 0; i < 3; i++) {
		send_message(REQUEST, offer.ip, offer.server);
		int got = await(ep, ACK, 2000u << i, l);
		if (got) {
			return got == ACK && l->ip; /* a NAK: start again */
		}
	}
	return false;
}

static void apply(const struct lease *l)
{
	uint32_t mask = l->netmask ? l->netmask : IP_ADDR(255, 255, 255, 0);
	uint32_t flags = cpu_irq_save();
	ifc->ip = l->ip;
	ifc->netmask = mask;
	ifc->gateway = l->gateway;
	cpu_irq_restore(flags);
	int prefix = 0;
	for (uint32_t m = mask; m & 0x80000000u; m <<= 1) {
		prefix++;
	}
	char a[16], g[16], s[16];
	kprintf("dhcp: %s: %s/%d, gateway %s (from %s, for %lu s)\n", ifc->name, ip_format(l->ip, a),
	        prefix, l->gateway ? ip_format(l->gateway, g) : "none", ip_format(l->server, s),
	        (unsigned long)l->seconds);
	leased = true;
}

static void dhcp_main(void *unused)
{
	(void)unused;
	int err;
	struct udp_endpoint *ep = udp_open(DHCP_CLIENT_PORT, &err);
	if (!ep) {
		kprintf("dhcp: cannot use udp port %d\n", DHCP_CLIENT_PORT);
		thread_exit();
	}
	uint32_t pause = 4;
	for (;;) {
		struct lease l;
		if (acquire(ep, &l)) {
			apply(&l);
			pause = 4;
			if (l.seconds == 0xFFFFFFFFu) {
				break; /* forever */
			}
			/* Asked again at half the lease (at least a minute). */
			uint32_t renew = l.seconds / 2 < 60 ? 60 : l.seconds / 2;
			while (renew) {
				uint32_t step = renew > 3600 ? 3600 : renew;
				timer_sleep_ms(step * 1000);
				renew -= step;
			}
		} else {
			timer_sleep_ms(pause * 1000);
			pause = pause < 64 ? pause * 2 : 64;
		}
	}
	udp_close(ep);
	thread_exit();
}

void dhcp_start(struct netif *which)
{
	ifc = which;
	if (!thread_create("dhcp", dhcp_main, 0)) {
		kprintf("dhcp: cannot start its thread\n");
	}
}
