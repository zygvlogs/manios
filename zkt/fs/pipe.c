/* Pipes (M12): a pair of connected ends; what is written to one end is
 * read from the other, in both directions. Each write is a message: a
 * read returns bytes from one message only, so a reader with a big
 * enough buffer gets whole messages (the ZRP channel transport relies on
 * this), and a shorter read leaves the rest of the message for the next
 * one (so byte streams lose nothing). A zero-length write sends an empty
 * message, which reads as end of file, as in Plan 9.
 *
 * Reading an end whose peer is gone gives end of file once the queue is
 * empty; writing to it gives EPIPE. Each direction holds PIPE_CAPACITY
 * bytes; a writer waits for room (a single bigger message is taken when
 * the queue is empty). */
#include "pipe.h"
#include <stdbool.h>
#include "cpu.h"
#include "heap.h"
#include "kerrno.h"
#include "kstring.h"
#include "poll.h"
#include "sched.h"

#define PIPE_CAPACITY (64 * 1024)
#define MESSAGE_MAX (256 * 1024)

struct message {
	struct message *next;
	size_t len, pos;
	uint8_t data[];
};

struct direction {
	struct message *head, *tail;
	size_t bytes;
	struct waitq readable, writable;
};

struct pipe;

struct pipe_end {
	struct vnode vnode;
	struct pipe *pipe;
	int side; /* reads dir[side], writes dir[1 - side] */
};

struct pipe {
	struct direction dir[2]; /* dir[i]: messages for end i to read */
	struct pipe_end end[2];
	bool open[2];
};

static const struct vnode_ops pipe_ops;

bool vnode_is_pipe(const struct vnode *v)
{
	return v->ops == &pipe_ops;
}

static void free_messages(struct direction *d)
{
	struct message *m = d->head;
	while (m) {
		struct message *next = m->next;
		kfree(m);
		m = next;
	}
	d->head = d->tail = 0;
	d->bytes = 0;
}

long pipe_recv(struct vnode *v, void *buf, size_t len, uint64_t deadline)
{
	struct pipe_end *e = (struct pipe_end *)v;
	struct pipe *p = e->pipe;
	struct direction *d = &p->dir[e->side];
	uint32_t flags = cpu_irq_save();
	while (!d->head && p->open[1 - e->side]) {
		if (!deadline) {
			waitq_sleep(&d->readable);
		} else if (!waitq_sleep_until(&d->readable, deadline)) {
			cpu_irq_restore(flags);
			return -ETIMEDOUT;
		}
	}
	struct message *m = d->head;
	if (!m) {
		cpu_irq_restore(flags);
		return 0; /* the writer is gone and everything is read */
	}
	size_t n = m->len - m->pos < len ? m->len - m->pos : len;
	memcpy(buf, m->data + m->pos, n); /* heap memory: no fault with interrupts off */
	m->pos += n;
	bool done = m->pos == m->len;
	if (done) {
		d->head = m->next;
		if (!d->head) {
			d->tail = 0;
		}
		d->bytes -= m->len;
	}
	cpu_irq_restore(flags);
	if (done) {
		kfree(m);
		waitq_wake_all(&d->writable);
	}
	return (long)n;
}

long pipe_send(struct vnode *v, const void *buf, size_t len)
{
	struct pipe_end *e = (struct pipe_end *)v;
	struct pipe *p = e->pipe;
	struct direction *d = &p->dir[1 - e->side];
	if (len > MESSAGE_MAX) {
		return -EINVAL;
	}
	struct message *m = kmalloc(sizeof(*m) + len);
	if (!m) {
		return -ENOMEM;
	}
	m->next = 0;
	m->len = len;
	m->pos = 0;
	memcpy(m->data, buf, len);

	uint32_t flags = cpu_irq_save();
	while (p->open[1 - e->side] && d->bytes && d->bytes + len > PIPE_CAPACITY) {
		waitq_sleep(&d->writable);
	}
	if (!p->open[1 - e->side]) {
		cpu_irq_restore(flags);
		kfree(m);
		return -EPIPE;
	}
	if (d->tail) {
		d->tail->next = m;
	} else {
		d->head = m;
	}
	d->tail = m;
	d->bytes += len;
	cpu_irq_restore(flags);
	waitq_wake_all(&d->readable);
	poll_notify();
	return (long)len;
}

static long end_read(struct vnode *v, uint32_t offset, void *buf, size_t len)
{
	(void)offset;
	return pipe_recv(v, buf, len, 0);
}

static long end_write(struct vnode *v, uint32_t offset, const void *buf, size_t len)
{
	(void)offset;
	return pipe_send(v, buf, len);
}

static int end_poll(struct vnode *v)
{
	struct pipe_end *e = (struct pipe_end *)v;
	return e->pipe->dir[e->side].head || !e->pipe->open[1 - e->side];
}

static void end_release(struct vnode *v)
{
	struct pipe_end *e = (struct pipe_end *)v;
	struct pipe *p = e->pipe;
	uint32_t flags = cpu_irq_save();
	p->open[e->side] = false;
	bool last = !p->open[1 - e->side];
	cpu_irq_restore(flags);
	/* The other end's reader sees end of file, its writer EPIPE. */
	waitq_wake_all(&p->dir[1 - e->side].readable);
	waitq_wake_all(&p->dir[e->side].writable);
	poll_notify();
	if (last) {
		free_messages(&p->dir[0]);
		free_messages(&p->dir[1]);
		kfree(p);
	}
}

static const struct vnode_ops pipe_ops = {
	.read = end_read,
	.write = end_write,
	.release = end_release,
	.poll = end_poll,
};

int pipe_create(struct vnode *ends[2])
{
	struct pipe *p = kmalloc(sizeof(*p));
	if (!p) {
		return -ENOMEM;
	}
	memset(p, 0, sizeof(*p));
	for (int i = 0; i < 2; i++) {
		p->dir[i].readable = (struct waitq)WAITQ_INIT;
		p->dir[i].writable = (struct waitq)WAITQ_INIT;
		p->end[i].vnode.ops = &pipe_ops;
		p->end[i].vnode.type = VNODE_PIPE;
		p->end[i].vnode.refs = 1;
		p->end[i].pipe = p;
		p->end[i].side = i;
		p->open[i] = true;
		ends[i] = &p->end[i].vnode;
	}
	return 0;
}
