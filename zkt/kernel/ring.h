/* Byte ring buffer. `size` must be a power of two; head and tail are
 * free-running counters, so full and empty are distinguishable without
 * wasting a slot. Not synchronized: callers hold interrupts off when an
 * IRQ handler is on the other end. */
#ifndef ZKT_KERNEL_RING_H
#define ZKT_KERNEL_RING_H

#include <stdbool.h>
#include <stdint.h>

struct ring {
	uint8_t *buf;
	uint32_t size;
	uint32_t head; /* next write */
	uint32_t tail; /* next read */
};

#define RING_INIT(storage) { (storage), sizeof(storage), 0, 0 }

static inline bool ring_empty(const struct ring *r)
{
	return r->head == r->tail;
}

static inline bool ring_full(const struct ring *r)
{
	return r->head - r->tail == r->size;
}

static inline void ring_push(struct ring *r, uint8_t c)
{
	r->buf[r->head++ & (r->size - 1)] = c;
}

static inline uint8_t ring_pop(struct ring *r)
{
	return r->buf[r->tail++ & (r->size - 1)];
}

#endif
