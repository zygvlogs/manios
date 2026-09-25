/* The ZRP client: a remote tree as a filesystem (docs/zrp.md). Each
 * vnode is a fid on the server; the vnode operations map one-to-one
 * onto requests (walk -> Twalk, read -> Tread, readdir -> Tread on a
 * directory, release -> Tclunk).
 *
 * Any number of requests may be in flight on a session, so a read the
 * server answers only later (a console waiting for a line, a window
 * waiting for input) doesn't hold up anyone else. Whichever waiting
 * thread is receiving reads the next reply and hands it to the request
 * with its tag; the others sleep. Two transports:
 *  - UDP (M10) may lose or reorder datagrams, so each request is sent
 *    again, with the same tag, until its reply comes; the server
 *    recognises the repeat (zrp_server.c), and answers Rpending while
 *    it is still working on it (M13).
 *  - A channel (M12) is a pipe end whose other end a userspace server
 *    reads (SYS_MOUNTFD). Nothing is lost: nothing is sent twice.
 *
 * With a key, attaching over UDP authenticates both ways (M13). */
#include "zrp.h"
#include <stdbool.h>
#include "cpu.h"
#include "heap.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "mutex.h"
#include "net.h"
#include "sha256.h"
#include "pipe.h"
#include "sched.h"
#include "timer.h"
#include "udp.h"
#include "zkt_abi.h"

#define TRIES 6
#define FIRST_TIMEOUT_MS 150 /* then doubling: 0.15 + 0.3 + ... + 4.8 s = 9.45 s */
#define PENDING_POLL_MS 1000 /* after Rpending: asking again, TRIES times at most */
#define DIR_CACHE 32
#define ROOT_FID 1

enum transport { UDP, CHANNEL };

/* A request in flight. */
struct call {
	uint16_t tag;
	uint8_t *rx;
	size_t cap;
	long len;     /* the reply's length once done, or a negated error */
	bool done;
	bool pending; /* UDP: the server said Rpending since we last sent */
	struct call *next;
};

struct session {
	uint32_t refs;
	enum transport transport;
	uint32_t msize;
	uint32_t next_fid;
	uint16_t tag;
	/* UDP */
	struct udp_endpoint *ep;
	uint32_t ip;
	uint16_t port;
	/* a channel */
	struct vnode *chan;
	/* interrupts off guards the fields below */
	struct call *calls;
	bool receiving;
	bool dead; /* the server is gone (a channel at end of file; UDP timed out) */
	struct waitq changed;
	uint8_t *inbox; /* the receiving thread's buffer */
};

struct znode {
	struct vnode v;
	struct session *s;
	uint32_t fid;
	/* Directory entries [cache_first, cache_first + cache_count), from
	 * the last Tread, so listing a directory costs one request per
	 * message-full of entries rather than one per entry. */
	struct mutex cache_lock;
	uint32_t cache_first, cache_count;
	struct dirent *cache;
};

static volatile uint32_t retransmits;

uint32_t zrp_client_retransmits(void)
{
	return retransmits;
}

static void session_ref(struct session *s)
{
	uint32_t flags = cpu_irq_save();
	s->refs++;
	cpu_irq_restore(flags);
}

static void session_unref(struct session *s)
{
	uint32_t flags = cpu_irq_save();
	bool last = --s->refs == 0;
	cpu_irq_restore(flags);
	if (!last) {
		return;
	}
	if (s->transport == UDP) {
		udp_close(s->ep);
	} else {
		vnode_unref(s->chan); /* the server reads end of file */
	}
	kfree(s->inbox);
	kfree(s);
}

/* The reply's header: its tag, and whether it is well formed. */
static bool reply_tag(const uint8_t *msg, long n, uint16_t *tag)
{
	if (n < ZRP_HEADER) {
		return false;
	}
	uint32_t size = (uint32_t)msg[0] | (uint32_t)msg[1] << 8 | (uint32_t)msg[2] << 16
	                | (uint32_t)msg[3] << 24;
	*tag = (uint16_t)(msg[5] | msg[6] << 8);
	return size == (uint32_t)n;
}

/* Checks a reply's type: its length, the server's error, or -EPROTO. */
static long check_reply(const uint8_t *msg, long n, uint8_t expect)
{
	if (n < 0) {
		return n;
	}
	if (msg[4] == ZRP_RERROR) {
		struct zmsg m;
		zmsg_open(&m, (uint8_t *)msg, (size_t)n);
		m.pos = ZRP_HEADER;
		uint16_t err = zmsg_get16(&m);
		return !m.bad && err && err <= ZKT_ERRNO_MAX ? -(long)err : -EPROTO;
	}
	return msg[4] == expect ? n : -EPROTO;
}

/* Converts milliseconds to timer ticks, rounding up (32-bit only). */
static uint32_t ticks_for(uint32_t ms)
{
	return (ms * TIMER_HZ + 999) / 1000;
}

/* Sends a request and waits for its reply, which the receiving thread
 * (see the top of this file) hands over. Over UDP the request is sent
 * again at each deadline until the reply comes: TRIES times, waiting
 * twice as long each time. Once the server has said Rpending -- it is
 * working on the request -- the client keeps asking every
 * PENDING_POLL_MS for as long as the server keeps saying so, so a read
 * that waits for minutes (a console, a window) doesn't time out, but a
 * server that goes away is noticed. */
static long rpc(struct session *s, struct call *c, const uint8_t *tx, size_t len, uint8_t expect)
{
	bool udp = s->transport == UDP;
	uint32_t timeout = FIRST_TIMEOUT_MS;
	int tries = 1;
	bool polling = false;
	if (s->dead) {
		return -EIO;
	}
	long sent = udp ? udp_send(s->ep, s->ip, s->port, tx, len) : pipe_send(s->chan, tx, len);
	uint64_t deadline = timer_ticks() + ticks_for(timeout);
	uint32_t flags = cpu_irq_save();
	if (sent < 0) {
		c->len = udp ? sent : -EIO;
		c->done = true;
	}
	while (!c->done) {
		if (s->dead) {
			c->len = -EIO;
			break;
		}
		if (udp && timer_ticks() >= deadline) {
			if (c->pending) {
				polling = true;
				tries = 0;
			} else if (tries == TRIES) {
				/* The server is gone: what else is waiting, and what
				 * comes later (the clunks as files close), fails at once. */
				c->len = -ETIMEDOUT;
				s->dead = true;
				waitq_wake_all(&s->changed);
				break;
			}
			c->pending = false;
			timeout = polling ? PENDING_POLL_MS : timeout * 2;
			tries++;
			cpu_irq_restore(flags);
			if (!polling) {
				retransmits++;
			}
			udp_send(s->ep, s->ip, s->port, tx, len);
			flags = cpu_irq_save();
			deadline = timer_ticks() + ticks_for(timeout);
			continue;
		}
		if (s->receiving) {
			if (udp) {
				waitq_sleep_until(&s->changed, deadline);
			} else {
				waitq_sleep(&s->changed);
			}
			continue;
		}
		/* Our turn to receive, for everyone. */
		s->receiving = true;
		cpu_irq_restore(flags);
		long n;
		uint16_t tag = 0;
		bool ok;
		if (udp) {
			uint64_t now = timer_ticks();
			uint32_t wait = deadline > now ? (uint32_t)(deadline - now) * (1000 / TIMER_HZ) : 1;
			uint32_t from;
			uint16_t from_port;
			n = udp_recv(s->ep, s->inbox, s->msize, &from, &from_port, wait);
			ok = n > 0 && from == s->ip && from_port == s->port && reply_tag(s->inbox, n, &tag);
		} else {
			n = pipe_recv(s->chan, s->inbox, s->msize, 0);
			ok = n > 0 && reply_tag(s->inbox, n, &tag);
		}
		flags = cpu_irq_save();
		struct call *owner = 0;
		if (!ok) {
			/* A channel at end of file: the server is gone. Over UDP,
			 * a timeout or a stray; a malformed reply is just dropped. */
			s->dead = !udp && n <= 0;
		} else {
			for (owner = s->calls; owner && owner->tag != tag; owner = owner->next) {
			}
		}
		if (owner && s->inbox[4] == ZRP_RPENDING) {
			owner->pending = true;
		} else if (owner && !owner->done) {
			/* The owner waits until done, so its buffer stays put. */
			cpu_irq_restore(flags);
			size_t k = (size_t)n < owner->cap ? (size_t)n : owner->cap;
			memcpy(owner->rx, s->inbox, k);
			flags = cpu_irq_save();
			owner->len = (long)k;
			owner->done = true;
		}
		s->receiving = false;
		waitq_wake_all(&s->changed);
	}
	cpu_irq_restore(flags);
	return c->len < 0 ? c->len : check_reply(c->rx, c->len, expect);
}

/* One request: its buffers, and the tag it goes out with. */
struct request {
	struct session *s;
	uint8_t *tx, *rx;
	struct zmsg m;
	struct call call;
};

/* A tag no request in flight uses. */
static uint16_t next_tag(struct session *s)
{
	for (;;) {
		s->tag = s->tag == ZRP_NOTAG - 1 ? 0 : s->tag + 1;
		struct call *c = s->calls;
		while (c && c->tag != s->tag) {
			c = c->next;
		}
		if (!c) {
			return s->tag;
		}
	}
}

static int begin(struct session *s, struct request *r, uint8_t type)
{
	r->s = s;
	r->tx = kmalloc(s->msize);
	r->rx = kmalloc(s->msize);
	if (!r->tx || !r->rx) {
		kfree(r->tx);
		kfree(r->rx);
		return -ENOMEM;
	}
	memset(&r->call, 0, sizeof(r->call));
	r->call.rx = r->rx;
	r->call.cap = s->msize;
	/* The tag is taken, and the call listed for its reply, in one step:
	 * no other request can pick the same tag meanwhile. */
	uint32_t flags = cpu_irq_save();
	uint16_t tag = type == ZRP_TVERSION ? ZRP_NOTAG : next_tag(s);
	r->call.tag = tag;
	r->call.next = s->calls;
	s->calls = &r->call;
	cpu_irq_restore(flags);
	zmsg_start(&r->m, r->tx, s->msize, type, tag);
	return 0;
}

/* Sends the request; the reply's length (in r->rx) or an error. */
static long send(struct request *r, uint8_t expect)
{
	size_t len = zmsg_finish(&r->m);
	if (!len) {
		return -EINVAL; /* too long for a message */
	}
	return rpc(r->s, &r->call, r->tx, len, expect);
}

static void end(struct request *r)
{
	struct session *s = r->s;
	uint32_t flags = cpu_irq_save();
	struct call **link = &s->calls;
	while (*link != &r->call) {
		link = &(*link)->next;
	}
	*link = r->call.next;
	cpu_irq_restore(flags);
	kfree(r->tx);
	kfree(r->rx);
}

/* A reader positioned after the reply's header. */
static void reply(struct request *r, long n, struct zmsg *m)
{
	zmsg_open(m, r->rx, (size_t)n);
	m->pos = ZRP_HEADER;
}

static const struct vnode_ops zrp_ops;

static struct znode *make_node(struct session *s, uint32_t fid, enum vnode_type type,
                               uint32_t length)
{
	struct znode *z = kmalloc(sizeof(*z));
	if (!z) {
		return 0;
	}
	memset(z, 0, sizeof(*z));
	z->v.ops = &zrp_ops;
	z->v.type = type;
	z->v.size = length;
	z->v.refs = 1;
	z->s = s;
	z->fid = fid;
	z->cache_lock = (struct mutex)MUTEX_INIT;
	session_ref(s);
	return z;
}

static struct znode *node_from_reply(struct request *r, long n, uint32_t fid)
{
	struct zmsg m;
	enum vnode_type type;
	uint32_t length;
	char name[VFS_NAME_MAX + 1];
	reply(r, n, &m);
	if (!zmsg_getstat(&m, &type, &length, name, sizeof(name))) {
		return 0;
	}
	return make_node(r->s, fid, type, length);
}

static void clunk(struct session *s, uint32_t fid)
{
	struct request r;
	if (begin(s, &r, ZRP_TCLUNK) == 0) {
		zmsg_put32(&r.m, fid);
		send(&r, ZRP_RCLUNK); /* nothing to do if it fails: the server drops fids with the session */
		end(&r);
	}
}

static uint32_t new_fid(struct session *s)
{
	uint32_t flags = cpu_irq_save();
	uint32_t fid = s->next_fid++;
	cpu_irq_restore(flags);
	return fid;
}

static int zrp_walk(struct vnode *dir, const char *name, struct vnode **out)
{
	struct znode *d = (struct znode *)dir;
	struct session *s = d->s;
	struct request r;
	int rc = begin(s, &r, ZRP_TWALK);
	if (rc) {
		return rc;
	}
	uint32_t fid = new_fid(s);
	zmsg_put32(&r.m, d->fid);
	zmsg_put32(&r.m, fid);
	zmsg_putstr(&r.m, name);
	long n = send(&r, ZRP_RWALK);
	struct znode *z = n < 0 ? 0 : node_from_reply(&r, n, fid);
	end(&r);
	if (n >= 0 && !z) {
		clunk(s, fid);
		n = -EPROTO;
	}
	if (n < 0) {
		return (int)n;
	}
	*out = &z->v;
	return 0;
}

/* One Tread (or Twrite) moves at most a message-full; the VFS callers
 * loop for more. */
static long zrp_read(struct vnode *v, uint32_t offset, void *buf, size_t len)
{
	struct znode *z = (struct znode *)v;
	struct request r;
	long n = begin(z->s, &r, ZRP_TREAD);
	if (n) {
		return n;
	}
	size_t room = z->s->msize - ZRP_READ_OVERHEAD;
	uint32_t count = (uint32_t)(len < room ? len : room);
	zmsg_put32(&r.m, z->fid);
	zmsg_put32(&r.m, offset);
	zmsg_put32(&r.m, count);
	n = send(&r, ZRP_RREAD);
	if (n >= 0) {
		struct zmsg m;
		reply(&r, n, &m);
		uint32_t got = zmsg_get32(&m);
		const uint8_t *data = zmsg_getbytes(&m, got);
		if (!data || got > count) {
			n = -EPROTO;
		} else {
			memcpy(buf, data, got);
			n = got;
		}
	}
	end(&r);
	return n;
}

static long zrp_write(struct vnode *v, uint32_t offset, const void *buf, size_t len)
{
	struct znode *z = (struct znode *)v;
	struct request r;
	long n = begin(z->s, &r, ZRP_TWRITE);
	if (n) {
		return n;
	}
	size_t room = z->s->msize - ZRP_WRITE_OVERHEAD;
	uint32_t count = (uint32_t)(len < room ? len : room);
	zmsg_put32(&r.m, z->fid);
	zmsg_put32(&r.m, offset);
	zmsg_put32(&r.m, count);
	zmsg_putbytes(&r.m, buf, count);
	n = send(&r, ZRP_RWRITE);
	if (n >= 0) {
		struct zmsg m;
		reply(&r, n, &m);
		uint32_t wrote = zmsg_get32(&m);
		n = m.bad || wrote > count ? -EPROTO : (long)wrote;
	}
	end(&r);
	return n;
}

static int zrp_readdir(struct vnode *dir, uint32_t index, struct dirent *out)
{
	struct znode *d = (struct znode *)dir;
	struct session *s = d->s;
	int rc = 0;
	mutex_lock(&d->cache_lock);
	if (!d->cache) {
		d->cache = kmalloc(DIR_CACHE * sizeof(struct dirent));
		rc = d->cache ? 0 : -ENOMEM;
	}
	if (rc == 0 && (index < d->cache_first || index >= d->cache_first + d->cache_count)) {
		struct request r;
		rc = begin(s, &r, ZRP_TREAD);
		if (rc == 0) {
			zmsg_put32(&r.m, d->fid);
			zmsg_put32(&r.m, index); /* directories count entries, not bytes */
			zmsg_put32(&r.m, s->msize - ZRP_READ_OVERHEAD);
			long n = send(&r, ZRP_RREAD);
			d->cache_first = index;
			d->cache_count = 0;
			if (n < 0) {
				rc = (int)n;
			} else {
				struct zmsg m;
				reply(&r, n, &m);
				uint32_t got = zmsg_get32(&m);
				size_t stop = m.pos + got;
				if (m.bad || stop != (size_t)n) {
					rc = -EPROTO;
				}
				while (rc == 0 && m.pos < stop && d->cache_count < DIR_CACHE) {
					struct dirent *e = &d->cache[d->cache_count];
					if (!zmsg_getstat(&m, &e->type, &e->size, e->name, sizeof(e->name))) {
						rc = -EPROTO;
					} else {
						d->cache_count++;
					}
				}
			}
			end(&r);
		}
	}
	if (rc == 0 && index - d->cache_first < d->cache_count) {
		*out = d->cache[index - d->cache_first];
		rc = 1;
	}
	mutex_unlock(&d->cache_lock);
	return rc;
}

static void zrp_release(struct vnode *v)
{
	struct znode *z = (struct znode *)v;
	struct session *s = z->s;
	clunk(s, z->fid);
	kfree(z->cache);
	kfree(z);
	session_unref(s);
}

static const struct vnode_ops zrp_ops = {
	.walk = zrp_walk,
	.read = zrp_read,
	.write = zrp_write,
	.readdir = zrp_readdir,
	.release = zrp_release,
};

static bool parse_dial(const char *dial, uint32_t *ip, uint16_t *port)
{
	char host[32];
	*port = ZRP_PORT;
	if (!strncmp(dial, "udp!", 4)) {
		dial += 4;
	}
	const char *bang = dial;
	while (*bang && *bang != '!') {
		bang++;
	}
	size_t n = (size_t)(bang - dial);
	if (n >= sizeof(host)) {
		return false;
	}
	memcpy(host, dial, n);
	host[n] = '\0';
	if (*bang) {
		uint32_t p = 0;
		const char *c = bang + 1;
		for (; *c >= '0' && *c <= '9' && p <= 65535; c++) {
			p = p * 10 + (uint32_t)(*c - '0');
		}
		if (*c || c == bang + 1 || p == 0 || p > 65535) {
			return false;
		}
		*port = (uint16_t)p;
	}
	return ip_parse(host, ip, 0);
}

/* Tauth: proves the server knows the key; fills in the nonces. */
static int authenticate(struct session *s, const uint8_t *key, uint8_t cnonce[ZRP_NONCE],
                        uint8_t snonce[ZRP_NONCE])
{
	struct request r;
	long n = begin(s, &r, ZRP_TAUTH);
	if (n) {
		return (int)n;
	}
	zrp_nonce(cnonce);
	zmsg_putbytes(&r.m, cnonce, ZRP_NONCE);
	n = send(&r, ZRP_RAUTH);
	int rc = n < 0 ? (n == -ETIMEDOUT || n == -ENOMEM ? (int)n : -EACCES) : 0;
	if (rc == 0) {
		struct zmsg m;
		reply(&r, n, &m);
		const uint8_t *nonce = zmsg_getbytes(&m, ZRP_NONCE);
		const uint8_t *mac = zmsg_getbytes(&m, ZRP_MAC);
		uint8_t want[ZRP_MAC];
		if (m.bad || m.pos != (size_t)n) {
			rc = -EPROTO;
		} else {
			memcpy(snonce, nonce, ZRP_NONCE);
			zrp_auth_mac(key, "server", cnonce, snonce, want);
			rc = equal_secret(mac, want, ZRP_MAC) ? 0 : -EACCES;
		}
	}
	end(&r);
	return rc;
}

/* Version (and, with a key, authentication) and attach on a new
 * session, which holds one reference, ours, dropped here; stores the
 * root. */
static int attach(struct session *s, const char *aname, const uint8_t *key, struct vnode **root)
{
	struct request r;
	long n = begin(s, &r, ZRP_TVERSION);
	if (n == 0) {
		uint32_t want = s->msize;
		zmsg_put32(&r.m, want);
		zmsg_putstr(&r.m, ZRP_VERSION);
		n = send(&r, ZRP_RVERSION);
		if (n >= 0) {
			struct zmsg m;
			char version[16];
			reply(&r, n, &m);
			uint32_t msize = zmsg_get32(&m);
			zmsg_getstr(&m, version, sizeof(version));
			if (m.bad || strcmp(version, ZRP_VERSION) || msize < ZRP_MSIZE_MIN || msize > want) {
				n = -EPROTO;
			} else {
				s->msize = msize; /* smaller from now on: buffers still fit */
			}
		}
		end(&r);
	}
	uint8_t cnonce[ZRP_NONCE], snonce[ZRP_NONCE];
	if (n >= 0 && key) {
		n = authenticate(s, key, cnonce, snonce);
	}
	struct znode *z = 0;
	if (n >= 0 && (n = begin(s, &r, ZRP_TATTACH)) == 0) {
		zmsg_put32(&r.m, ROOT_FID);
		zmsg_putstr(&r.m, aname ? aname : "");
		if (key) {
			uint8_t mac[ZRP_MAC];
			zrp_auth_mac(key, "client", cnonce, snonce, mac);
			zmsg_putbytes(&r.m, mac, ZRP_MAC);
		}
		n = send(&r, ZRP_RATTACH);
		z = n < 0 ? 0 : node_from_reply(&r, n, ROOT_FID);
		end(&r);
		if (n >= 0 && (!z || z->v.type != VNODE_DIR)) {
			if (z) {
				vnode_unref(&z->v); /* clunks */
				z = 0;
			}
			n = -ENOTDIR;
		}
	}
	session_unref(s);
	if (n < 0) {
		return (int)n;
	}
	*root = &z->v;
	return 0;
}

static struct session *new_session(enum transport transport, uint32_t msize)
{
	struct session *s = kmalloc(sizeof(*s));
	if (s) {
		memset(s, 0, sizeof(*s));
		s->refs = 1;
		s->transport = transport;
		s->msize = msize;
		s->next_fid = ROOT_FID + 1;
		s->changed = (struct waitq)WAITQ_INIT;
		s->inbox = kmalloc(msize);
		if (!s->inbox) {
			kfree(s);
			s = 0;
		}
	}
	return s;
}

int zrp_mount_key(const char *dial, const char *aname, const uint8_t *key, struct vnode **root,
                  char *label, size_t size)
{
	uint32_t ip;
	uint16_t port;
	if (!parse_dial(dial, &ip, &port)) {
		return -EINVAL;
	}
	struct session *s = new_session(UDP, ZRP_MSIZE);
	if (!s) {
		return -ENOMEM;
	}
	s->ip = ip;
	s->port = port;
	int rc;
	s->ep = udp_open(0, &rc);
	if (!s->ep) {
		kfree(s->inbox);
		kfree(s);
		return rc;
	}
	char host[16];
	ksnprintf(label, size, "zrp:%s!%u", ip_format(ip, host), (unsigned)port);
	return attach(s, aname, key, root);
}

int zrp_mount(const char *dial, const char *aname, struct vnode **root, char *label, size_t size)
{
	return zrp_mount_key(dial, aname, zrp_key(), root, label, size);
}

int zrp_mount_channel(struct vnode *chan, const char *aname, struct vnode **root, char *label,
                      size_t size)
{
	if (!vnode_is_pipe(chan)) {
		return -EINVAL;
	}
	struct session *s = new_session(CHANNEL, ZRP_CHANNEL_MSIZE);
	if (!s) {
		return -ENOMEM;
	}
	vnode_ref(chan);
	s->chan = chan;
	ksnprintf(label, size, "zrp:channel");
	return attach(s, aname, 0, root); /* local: no authentication */
}
