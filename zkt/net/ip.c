/* IPv4 (RFC 791) without fragmentation, and ICMP echo (RFC 792). */
#include "net.h"
#include "cpu.h"
#include "heap.h"
#include "kerrno.h"
#include "kstring.h"
#include "mutex.h"
#include "sched.h"
#include "timer.h"

#define PROTO_ICMP 1
#define PROTO_UDP  17
#define IP_FLAG_DF 0x4000
#define IP_FRAGMENT_BITS 0x3FFF /* MF, and the fragment offset */
#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8
#define PING_ID 0x5A4B
#define PING_DATA 32

static uint16_t next_id;

static bool is_local(uint32_t ip)
{
	if ((ip >> 24) == 127) {
		return true;
	}
	for (struct netif *ifc = netif_first(); ifc; ifc = ifc->next) {
		if (ifc->ip && ifc->ip == ip) {
			return true;
		}
	}
	return false;
}

/* The interface a packet to dst leaves by, and its next hop. */
static struct netif *route(uint32_t dst, uint32_t *next_hop)
{
	struct netif *fallback = 0;
	for (struct netif *ifc = netif_first(); ifc; ifc = ifc->next) {
		if (ifc->loopback || !ifc->ip) {
			continue;
		}
		if (((dst ^ ifc->ip) & ifc->netmask) == 0) {
			*next_hop = dst;
			return ifc;
		}
		if (!fallback && ifc->gateway) {
			fallback = ifc;
		}
	}
	if (fallback) {
		*next_hop = fallback->gateway;
	}
	return fallback;
}

uint32_t ip_source_for(uint32_t dst)
{
	if ((dst >> 24) == 127) {
		return IP_LOOPBACK;
	}
	if (is_local(dst)) {
		return dst;
	}
	uint32_t hop;
	struct netif *ifc = route(dst, &hop);
	return ifc ? ifc->ip : 0;
}

int ip_output(uint32_t dst, uint8_t protocol, const uint8_t *payload, size_t len)
{
	if (len > ETH_MTU - IP_HEADER) {
		return -EINVAL;
	}
	uint8_t *frame = kmalloc(ETH_HEADER + IP_HEADER + len);
	if (!frame) {
		return -ENOMEM;
	}
	uint8_t *h = frame + ETH_HEADER;
	uint32_t flags = cpu_irq_save();
	uint16_t id = next_id++;
	cpu_irq_restore(flags);
	h[0] = 0x45; /* version 4, 5-word header */
	h[1] = 0;
	put_be16(h + 2, (uint16_t)(IP_HEADER + len));
	put_be16(h + 4, id);
	put_be16(h + 6, IP_FLAG_DF);
	h[8] = 64; /* TTL */
	h[9] = protocol;
	put_be16(h + 10, 0);
	put_be32(h + 12, ip_source_for(dst));
	put_be32(h + 16, dst);
	put_be16(h + 10, inet_fold(inet_sum(0, h, IP_HEADER)));
	memcpy(h + IP_HEADER, payload, len);

	int rc;
	if (is_local(dst)) {
		rc = loopback_queue(h, IP_HEADER + len);
	} else {
		uint32_t hop;
		struct netif *ifc = route(dst, &hop);
		rc = ifc ? arp_resolve(ifc, hop, frame) : -ENETUNREACH;
		if (rc == 0) {
			memcpy(frame + 6, ifc->mac, ETH_ALEN);
			put_be16(frame + 12, 0x0800);
			rc = ifc->transmit(ifc, frame, ETH_HEADER + IP_HEADER + len);
			if (rc == 0) {
				ifc->tx_frames++;
			} else {
				ifc->tx_errors++;
			}
		}
	}
	kfree(frame);
	return rc;
}

void ip_input(struct netif *ifc, const uint8_t *p, size_t len)
{
	net_stats.ip_in++;
	size_t ihl = len ? (size_t)(p[0] & 0x0F) * 4 : 0;
	size_t total = len >= 4 ? get_be16(p + 2) : 0;
	if (len < IP_HEADER || (p[0] >> 4) != 4 || ihl < IP_HEADER || ihl > len || total < ihl
	    || total > len || inet_fold(inet_sum(0, p, ihl)) != 0) {
		net_stats.ip_bad++;
		return;
	}
	if (get_be16(p + 6) & IP_FRAGMENT_BITS) {
		net_stats.ip_fragments++; /* no reassembly: ZKT never sends fragments either */
		return;
	}
	uint32_t src = get_be32(p + 12), dst = get_be32(p + 16);
	if (!ifc->loopback && dst != ifc->ip) {
		net_stats.ip_not_ours++;
		return;
	}
	switch (p[9]) {
	case PROTO_ICMP:
		icmp_input(src, dst, p + ihl, total - ihl);
		break;
	case PROTO_UDP:
		udp_input(src, dst, p + ihl, total - ihl);
		break;
	default:
		break;
	}
}

/* --- ICMP echo --- */

static struct mutex ping_lock = MUTEX_INIT;
static struct waitq ping_wq = WAITQ_INIT;
static volatile bool ping_waiting, ping_answered;
static volatile uint16_t ping_seq;
static volatile uint32_t ping_from;

void icmp_input(uint32_t src, uint32_t dst, const uint8_t *data, size_t len)
{
	(void)dst;
	if (len < 8 || inet_fold(inet_sum(0, data, len)) != 0) {
		return;
	}
	if (data[0] == ICMP_ECHO_REQUEST && data[1] == 0 && len <= ETH_MTU - IP_HEADER) {
		uint8_t *reply = kmalloc(len);
		if (!reply) {
			return;
		}
		memcpy(reply, data, len);
		reply[0] = ICMP_ECHO_REPLY;
		put_be16(reply + 2, 0);
		put_be16(reply + 2, inet_fold(inet_sum(0, reply, len)));
		if (ip_output(src, PROTO_ICMP, reply, len) == 0) {
			net_stats.icmp_echo_replied++;
		}
		kfree(reply);
	} else if (data[0] == ICMP_ECHO_REPLY && get_be16(data + 4) == PING_ID) {
		uint32_t flags = cpu_irq_save();
		if (ping_waiting && get_be16(data + 6) == ping_seq && src == ping_from) {
			ping_answered = true;
			waitq_wake_all(&ping_wq);
		}
		cpu_irq_restore(flags);
	}
}

long icmp_ping(uint32_t dst, uint16_t seq, uint32_t timeout_ms)
{
	uint8_t msg[8 + PING_DATA];
	msg[0] = ICMP_ECHO_REQUEST;
	msg[1] = 0;
	put_be16(msg + 2, 0);
	put_be16(msg + 4, PING_ID);
	put_be16(msg + 6, seq);
	for (int i = 0; i < PING_DATA; i++) {
		msg[8 + i] = (uint8_t)('a' + i % 26);
	}
	put_be16(msg + 2, inet_fold(inet_sum(0, msg, sizeof(msg))));

	mutex_lock(&ping_lock);
	uint32_t flags = cpu_irq_save();
	ping_seq = seq;
	ping_from = dst;
	ping_answered = false;
	ping_waiting = true;
	cpu_irq_restore(flags);

	uint64_t start = timer_uptime_ms();
	long rc = ip_output(dst, PROTO_ICMP, msg, sizeof(msg));
	if (rc == 0) {
		uint64_t deadline = timer_ticks() + (timeout_ms * TIMER_HZ + 999) / 1000;
		flags = cpu_irq_save();
		while (!ping_answered && waitq_sleep_until(&ping_wq, deadline)) {
		}
		rc = ping_answered ? (long)(timer_uptime_ms() - start) : -ETIMEDOUT;
		ping_waiting = false;
		cpu_irq_restore(flags);
	}
	mutex_unlock(&ping_lock);
	return rc;
}
