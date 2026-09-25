/* The ZRP client: a remote tree as a filesystem (docs/zrp.md). Each
 * vnode is a fid on the server; the vnode operations map one-to-one
 * onto requests (walk -> Twalk, read -> Tread, readdir -> Tread on a
 * directory, release -> Tclunk).
 *
 * UDP may lose or reorder datagrams, so a session keeps one request in
 * flight at a time and retransmits it, with the same tag, until the
 * reply comes; the server recognises the repeat (zrp_server.c). */
#include "zrp.h"
#include <stdbool.h>
#include "cpu.h"
#include "heap.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "mutex.h"
#include "net.h"
#include "timer.h"
#include "udp.h"
#include "zkt_abi.h"

#define TRIES 6
#define FIRST_TIMEOUT_MS 150 /* then doubling: 0.15 + 0.3 + ... + 4.8 s = 9.45 s */
#define DIR_CACHE 32
#define ROOT_FID 1

struct session {
	uint32_t refs;
	struct mutex lock;
	struct udp_endpoint *ep;
	uint32_t ip;
	uint16_t port;
	uint16_t tag;
	uint32_t next_fid;
	uint32_t msize;
	uint8_t tx[ZRP_MSIZE], rx[ZRP_MSIZE];
};

struct znode {
	struct vnode v;
	struct session *s;
	uint32_t fid;
	/* Directory entries [cache_first, cache_first + cache_count), from
	 * the last Tread, so listing a directory costs one request per
	 * message-full of entries rather than one per entry. */
	uint32_t cache_first, cache_count;
	struct dirent *cache;
};

static volatile uint32_t retransmits;

uint32_t zrp_client_retransmits(void)
{
	return retransmits;
}

static void session_unref(struct session *s)
{
	uint32_t flags = cpu_irq_save();
	bool last = --s->refs == 0;
	cpu_irq_restore(flags);
	if (last) {
		udp_close(s->ep);
		kfree(s);
	}
}

/* Sends the message in s->tx and waits for its reply in s->rx, checking
 * the tag and that the type is `expect` or Rerror. Returns the reply's
 * length, or a negated error (the server's, for an Rerror). */
static long rpc(struct session *s, size_t len, uint8_t expect)
{
	uint16_t tag = (uint16_t)(s->tx[5] | s->tx[6] << 8);
	uint32_t timeout = FIRST_TIMEOUT_MS;
	for (int attempt = 0; attempt < TRIES; attempt++, timeout *= 2) {
		if (attempt) {
			retransmits++;
		}
		int rc = udp_send(s->ep, s->ip, s->port, s->tx, len);
		if (rc) {
			return rc;
		}
		uint64_t give_up = timer_uptime_ms() + timeout;
		for (;;) {
			uint64_t now = timer_uptime_ms();
			if (now >= give_up) {
				break;
			}
			uint32_t from;
			uint16_t from_port;
			long n = udp_recv(s->ep, s->rx, sizeof(s->rx), &from, &from_port,
			                  (uint32_t)(give_up - now));
			if (n == -ETIMEDOUT) {
				break;
			}
			struct zmsg m;
			zmsg_open(&m, s->rx, n > 0 ? (size_t)n : 0);
			uint32_t size = zmsg_get32(&m);
			uint8_t type = zmsg_get8(&m);
			if (n < ZRP_HEADER || from != s->ip || from_port != s->port || size != (uint32_t)n
			    || zmsg_get16(&m) != tag) {
				continue; /* a stray, or a late reply to an earlier try */
			}
			if (type == ZRP_RERROR) {
				uint16_t err = zmsg_get16(&m);
				return !m.bad && err && err <= ZKT_ERRNO_MAX ? -(long)err : -EPROTO;
			}
			return type == expect ? n : -EPROTO;
		}
	}
	return -ETIMEDOUT;
}

static void start(struct session *s, struct zmsg *m, uint8_t type)
{
	s->tag = s->tag == ZRP_NOTAG - 1 ? 0 : s->tag + 1;
	zmsg_start(m, s->tx, s->msize, type, s->tag);
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
	uint32_t flags = cpu_irq_save();
	s->refs++;
	cpu_irq_restore(flags);
	return z;
}

/* Parses the stat in a reply of `len` bytes into a new node for fid. */
static struct znode *node_from_reply(struct session *s, long len, uint32_t fid)
{
	struct zmsg m;
	enum vnode_type type;
	uint32_t length;
	char name[VFS_NAME_MAX + 1];
	zmsg_open(&m, s->rx, (size_t)len);
	m.pos = ZRP_HEADER;
	if (!zmsg_getstat(&m, &type, &length, name, sizeof(name))) {
		return 0;
	}
	return make_node(s, fid, type, length);
}

static int clunk(struct session *s, uint32_t fid)
{
	struct zmsg m;
	start(s, &m, ZRP_TCLUNK);
	zmsg_put32(&m, fid);
	long n = rpc(s, zmsg_finish(&m), ZRP_RCLUNK);
	return n < 0 ? (int)n : 0;
}

static int zrp_walk(struct vnode *dir, const char *name, struct vnode **out)
{
	struct znode *d = (struct znode *)dir;
	struct session *s = d->s;
	mutex_lock(&s->lock);
	uint32_t fid = s->next_fid++;
	struct zmsg m;
	start(s, &m, ZRP_TWALK);
	zmsg_put32(&m, d->fid);
	zmsg_put32(&m, fid);
	zmsg_putstr(&m, name);
	long n = rpc(s, zmsg_finish(&m), ZRP_RWALK);
	struct znode *z = n < 0 ? 0 : node_from_reply(s, n, fid);
	if (n >= 0 && !z) {
		clunk(s, fid);
		n = -EPROTO;
	}
	mutex_unlock(&s->lock);
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
	struct session *s = z->s;
	mutex_lock(&s->lock);
	size_t room = s->msize - ZRP_READ_OVERHEAD;
	uint32_t count = (uint32_t)(len < room ? len : room);
	struct zmsg m;
	start(s, &m, ZRP_TREAD);
	zmsg_put32(&m, z->fid);
	zmsg_put32(&m, offset);
	zmsg_put32(&m, count);
	long n = rpc(s, zmsg_finish(&m), ZRP_RREAD);
	if (n >= 0) {
		zmsg_open(&m, s->rx, (size_t)n);
		m.pos = ZRP_HEADER;
		uint32_t got = zmsg_get32(&m);
		const uint8_t *data = zmsg_getbytes(&m, got);
		if (!data || got > count) {
			n = -EPROTO;
		} else {
			memcpy(buf, data, got);
			n = got;
		}
	}
	mutex_unlock(&s->lock);
	return n;
}

static long zrp_write(struct vnode *v, uint32_t offset, const void *buf, size_t len)
{
	struct znode *z = (struct znode *)v;
	struct session *s = z->s;
	mutex_lock(&s->lock);
	size_t room = s->msize - ZRP_WRITE_OVERHEAD;
	uint32_t count = (uint32_t)(len < room ? len : room);
	struct zmsg m;
	start(s, &m, ZRP_TWRITE);
	zmsg_put32(&m, z->fid);
	zmsg_put32(&m, offset);
	zmsg_put32(&m, count);
	zmsg_putbytes(&m, buf, count);
	long n = rpc(s, zmsg_finish(&m), ZRP_RWRITE);
	if (n >= 0) {
		zmsg_open(&m, s->rx, (size_t)n);
		m.pos = ZRP_HEADER;
		uint32_t wrote = zmsg_get32(&m);
		n = m.bad || wrote > count ? -EPROTO : (long)wrote;
	}
	mutex_unlock(&s->lock);
	return n;
}

static int zrp_readdir(struct vnode *dir, uint32_t index, struct dirent *out)
{
	struct znode *d = (struct znode *)dir;
	struct session *s = d->s;
	mutex_lock(&s->lock);
	int rc = 0;
	if (!d->cache) {
		d->cache = kmalloc(DIR_CACHE * sizeof(struct dirent));
		rc = d->cache ? 0 : -ENOMEM;
	}
	if (rc == 0 && (index < d->cache_first || index >= d->cache_first + d->cache_count)) {
		struct zmsg m;
		start(s, &m, ZRP_TREAD);
		zmsg_put32(&m, d->fid);
		zmsg_put32(&m, index); /* directories count entries, not bytes */
		zmsg_put32(&m, s->msize - ZRP_READ_OVERHEAD);
		long n = rpc(s, zmsg_finish(&m), ZRP_RREAD);
		d->cache_first = index;
		d->cache_count = 0;
		if (n < 0) {
			rc = (int)n;
		} else {
			zmsg_open(&m, s->rx, (size_t)n);
			m.pos = ZRP_HEADER;
			uint32_t got = zmsg_get32(&m);
			size_t end = m.pos + got;
			if (m.bad || end != (size_t)n) {
				rc = -EPROTO;
			}
			while (rc == 0 && m.pos < end && d->cache_count < DIR_CACHE) {
				struct dirent *e = &d->cache[d->cache_count];
				if (!zmsg_getstat(&m, &e->type, &e->size, e->name, sizeof(e->name))) {
					rc = -EPROTO;
				} else {
					d->cache_count++;
				}
			}
		}
	}
	if (rc == 0 && index - d->cache_first < d->cache_count) {
		*out = d->cache[index - d->cache_first];
		rc = 1;
	}
	mutex_unlock(&s->lock);
	return rc;
}

static void zrp_release(struct vnode *v)
{
	struct znode *z = (struct znode *)v;
	struct session *s = z->s;
	mutex_lock(&s->lock);
	clunk(s, z->fid); /* nothing to do if it fails: the server drops fids with the session */
	mutex_unlock(&s->lock);
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

int zrp_mount(const char *dial, const char *aname, struct vnode **root, char *label, size_t size)
{
	uint32_t ip;
	uint16_t port;
	if (!parse_dial(dial, &ip, &port)) {
		return -EINVAL;
	}
	struct session *s = kmalloc(sizeof(*s));
	if (!s) {
		return -ENOMEM;
	}
	memset(s, 0, sizeof(*s));
	s->lock = (struct mutex)MUTEX_INIT;
	s->refs = 1; /* ours, until the root node holds one */
	s->ip = ip;
	s->port = port;
	s->msize = ZRP_MSIZE;
	s->next_fid = ROOT_FID + 1;
	int rc;
	s->ep = udp_open(0, &rc);
	if (!s->ep) {
		kfree(s);
		return rc;
	}

	struct zmsg m;
	zmsg_start(&m, s->tx, s->msize, ZRP_TVERSION, ZRP_NOTAG);
	zmsg_put32(&m, ZRP_MSIZE);
	zmsg_putstr(&m, ZRP_VERSION);
	long n = rpc(s, zmsg_finish(&m), ZRP_RVERSION);
	if (n >= 0) {
		char version[16];
		zmsg_open(&m, s->rx, (size_t)n);
		m.pos = ZRP_HEADER;
		uint32_t msize = zmsg_get32(&m);
		zmsg_getstr(&m, version, sizeof(version));
		if (m.bad || strcmp(version, ZRP_VERSION) || msize < ZRP_MSIZE_MIN || msize > ZRP_MSIZE) {
			n = -EPROTO;
		} else {
			s->msize = msize;
		}
	}
	struct znode *z = 0;
	if (n >= 0) {
		start(s, &m, ZRP_TATTACH);
		zmsg_put32(&m, ROOT_FID);
		zmsg_putstr(&m, aname ? aname : "");
		n = rpc(s, zmsg_finish(&m), ZRP_RATTACH);
		z = n < 0 ? 0 : node_from_reply(s, n, ROOT_FID);
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
	char host[16];
	ksnprintf(label, size, "zrp:%s!%u", ip_format(ip, host), (unsigned)port);
	*root = &z->v;
	return 0;
}
