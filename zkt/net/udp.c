/* UDP (RFC 768) endpoints for kernel code. */
#include "udp.h"
#include "cpu.h"
#include "heap.h"
#include "kerrno.h"
#include "kstring.h"
#include "net.h"
#include "sched.h"
#include "timer.h"

#define PROTO_UDP 17
#define QUEUE_MAX 16
#define EPHEMERAL_FIRST 49152

struct datagram {
	struct datagram *next;
	uint32_t src;
	uint16_t src_port;
	size_t len;
	uint8_t data[];
};

struct udp_endpoint {
	uint16_t port;
	struct datagram *head, *tail;
	size_t queued;
	struct waitq arrived;
	struct udp_endpoint *next;
};

/* Interrupts off guards the list and the queues: the network thread
 * delivers while other threads send and receive. */
static struct udp_endpoint *endpoints;
static uint16_t next_ephemeral = EPHEMERAL_FIRST;

static struct udp_endpoint *find(uint16_t port)
{
	for (struct udp_endpoint *ep = endpoints; ep; ep = ep->next) {
		if (ep->port == port) {
			return ep;
		}
	}
	return 0;
}

struct udp_endpoint *udp_open(uint16_t port, int *err)
{
	struct udp_endpoint *ep = kmalloc(sizeof(*ep));
	if (!ep) {
		*err = -ENOMEM;
		return 0;
	}
	memset(ep, 0, sizeof(*ep));
	ep->arrived = (struct waitq)WAITQ_INIT;
	uint32_t flags = cpu_irq_save();
	if (port == 0) {
		for (unsigned tries = 0; tries < 65536 - EPHEMERAL_FIRST && !port; tries++) {
			uint16_t candidate = next_ephemeral;
			next_ephemeral = next_ephemeral == 65535 ? EPHEMERAL_FIRST : next_ephemeral + 1;
			if (!find(candidate)) {
				port = candidate;
			}
		}
	} else if (find(port)) {
		port = 0;
	}
	if (!port) {
		cpu_irq_restore(flags);
		kfree(ep);
		*err = -EADDRINUSE;
		return 0;
	}
	ep->port = port;
	ep->next = endpoints;
	endpoints = ep;
	cpu_irq_restore(flags);
	return ep;
}

uint16_t udp_port(const struct udp_endpoint *ep)
{
	return ep->port;
}

void udp_close(struct udp_endpoint *ep)
{
	uint32_t flags = cpu_irq_save();
	struct udp_endpoint **link = &endpoints;
	while (*link && *link != ep) {
		link = &(*link)->next;
	}
	if (*link) {
		*link = ep->next;
	}
	struct datagram *d = ep->head;
	ep->head = ep->tail = 0;
	cpu_irq_restore(flags);
	while (d) {
		struct datagram *next = d->next;
		kfree(d);
		d = next;
	}
	kfree(ep);
}

void udp_input(uint32_t src, uint32_t dst, const uint8_t *data, size_t len)
{
	net_stats.udp_in++;
	size_t ulen = len >= UDP_HEADER ? get_be16(data + 4) : 0;
	if (ulen < UDP_HEADER || ulen > len) {
		net_stats.udp_bad++;
		return;
	}
	/* A zero checksum means the sender didn't compute one (IPv4 only). */
	if (get_be16(data + 6)) {
		uint8_t pseudo[12];
		put_be32(pseudo, src);
		put_be32(pseudo + 4, dst);
		pseudo[8] = 0;
		pseudo[9] = PROTO_UDP;
		put_be16(pseudo + 10, (uint16_t)ulen);
		if (inet_fold(inet_sum(inet_sum(0, pseudo, 12), data, ulen)) != 0) {
			net_stats.udp_bad++;
			return;
		}
	}
	size_t payload = ulen - UDP_HEADER;
	struct datagram *d = kmalloc(sizeof(*d) + payload);
	if (!d) {
		return;
	}
	d->next = 0;
	d->src = src;
	d->src_port = get_be16(data);
	d->len = payload;
	memcpy(d->data, data + UDP_HEADER, payload);

	uint32_t flags = cpu_irq_save();
	struct udp_endpoint *ep = find(get_be16(data + 2));
	if (!ep || ep->queued == QUEUE_MAX) {
		cpu_irq_restore(flags);
		net_stats.udp_no_port += !ep;
		kfree(d);
		return;
	}
	if (ep->tail) {
		ep->tail->next = d;
	} else {
		ep->head = d;
	}
	ep->tail = d;
	ep->queued++;
	waitq_wake_all(&ep->arrived);
	cpu_irq_restore(flags);
}

int udp_send(struct udp_endpoint *ep, uint32_t dst, uint16_t dst_port, const void *data,
             size_t len)
{
	if (len > UDP_PAYLOAD_MAX) {
		return -EINVAL;
	}
	uint8_t *u = kmalloc(UDP_HEADER + len);
	if (!u) {
		return -ENOMEM;
	}
	put_be16(u, ep->port);
	put_be16(u + 2, dst_port);
	put_be16(u + 4, (uint16_t)(UDP_HEADER + len));
	put_be16(u + 6, 0);
	memcpy(u + UDP_HEADER, data, len);

	uint8_t pseudo[12];
	put_be32(pseudo, ip_source_for(dst));
	put_be32(pseudo + 4, dst);
	pseudo[8] = 0;
	pseudo[9] = PROTO_UDP;
	put_be16(pseudo + 10, (uint16_t)(UDP_HEADER + len));
	uint16_t sum = inet_fold(inet_sum(inet_sum(0, pseudo, 12), u, UDP_HEADER + len));
	put_be16(u + 6, sum ? sum : 0xFFFF); /* 0 would mean "no checksum" */

	int rc = ip_output(dst, PROTO_UDP, u, UDP_HEADER + len);
	kfree(u);
	return rc;
}

long udp_recv(struct udp_endpoint *ep, void *buf, size_t len, uint32_t *src, uint16_t *src_port,
              uint32_t timeout_ms)
{
	uint64_t deadline = timer_ticks() + (timeout_ms * TIMER_HZ + 999) / 1000;
	uint32_t flags = cpu_irq_save();
	while (!ep->head && waitq_sleep_until(&ep->arrived, deadline)) {
	}
	struct datagram *d = ep->head;
	if (d) {
		ep->head = d->next;
		if (!ep->head) {
			ep->tail = 0;
		}
		ep->queued--;
	}
	cpu_irq_restore(flags);
	if (!d) {
		return -ETIMEDOUT;
	}
	size_t n = d->len < len ? d->len : len;
	memcpy(buf, d->data, n);
	if (src) {
		*src = d->src;
	}
	if (src_port) {
		*src_port = d->src_port;
	}
	kfree(d);
	return (long)n;
}
