/* UDP endpoints for kernel code (the ZRP client and server). */
#ifndef ZKT_NET_UDP_H
#define ZKT_NET_UDP_H

#include <stddef.h>
#include <stdint.h>

struct udp_endpoint;

/* Binds `port` (0: an unused one from 49152 up). NULL with *err set to
 * -EADDRINUSE or -ENOMEM. */
struct udp_endpoint *udp_open(uint16_t port, int *err);
/* Unbinds and frees the endpoint, with any queued datagrams. Only its
 * owner may call it, and not while in udp_recv. */
void udp_close(struct udp_endpoint *ep);
uint16_t udp_port(const struct udp_endpoint *ep);

int udp_send(struct udp_endpoint *ep, uint32_t dst, uint16_t dst_port, const void *data,
             size_t len);
/* Waits up to timeout_ms for a datagram; returns its length (truncated
 * to len), or -ETIMEDOUT. At most 16 datagrams wait; more are dropped. */
long udp_recv(struct udp_endpoint *ep, void *buf, size_t len, uint32_t *src, uint16_t *src_port,
              uint32_t timeout_ms);

#endif
