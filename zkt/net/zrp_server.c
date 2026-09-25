/* The ZRP server: serves one directory tree of a namespace over UDP
 * (docs/zrp.md). One kernel thread handles requests in arrival order.
 *
 * A client is identified by its address and port. Each client may
 * have one request outstanding, and retransmits it until answered; so
 * the server keeps each session's last request and reply, and answers a
 * repeat of that request with the saved reply instead of performing
 * it again (a repeated walk or clunk must not fail the second time). */
#include "zrp.h"
#include "cpu.h"
#include "heap.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "namespace.h"
#include "net.h"
#include "sched.h"
#include "timer.h"
#include "udp.h"
#include "zkt_abi.h"

#define SESSIONS 8
#define FIDS 32
#define RECV_TIMEOUT_MS 200 /* how often the thread checks for stop */

struct fid {
	bool used;
	uint32_t id;
	char path[VFS_PATH_MAX + 1];
	struct file *rd; /* opened by the walk that made the fid */
	struct file *wr; /* opened by the first write */
};

struct session {
	bool used;
	uint32_t ip;
	uint16_t port;
	uint64_t last_active;
	uint32_t msize;
	struct fid fids[FIDS];
	uint8_t *last_request, *last_reply; /* ZRP_MSIZE each */
	size_t last_request_len, last_reply_len;
};

struct zrp_server {
	char root[VFS_PATH_MAX + 1];
	struct namespace *ns;
	struct udp_endpoint *ep;
	volatile bool stop, stopped;
	struct waitq done;
	struct session sessions[SESSIONS];
	struct zrp_server_stats stats;
	uint8_t request[ZRP_MSIZE], reply[ZRP_MSIZE];
};

static void clunk(struct fid *f)
{
	if (f->rd) {
		vfs_close(f->rd);
	}
	if (f->wr) {
		vfs_close(f->wr);
	}
	memset(f, 0, sizeof(*f));
}

static void session_reset(struct session *s)
{
	for (int i = 0; i < FIDS; i++) {
		if (s->fids[i].used) {
			clunk(&s->fids[i]);
		}
	}
	s->last_request_len = s->last_reply_len = 0;
}

static struct fid *find_fid(struct session *s, uint32_t id)
{
	for (int i = 0; i < FIDS; i++) {
		if (s->fids[i].used && s->fids[i].id == id) {
			return &s->fids[i];
		}
	}
	return 0;
}

/* A free slot for fid `id`: -EINVAL if it is in use, -EMFILE if full. */
static int new_fid(struct session *s, uint32_t id, struct fid **out)
{
	if (find_fid(s, id)) {
		return -EINVAL;
	}
	for (int i = 0; i < FIDS; i++) {
		if (!s->fids[i].used) {
			*out = &s->fids[i];
			return 0;
		}
	}
	return -EMFILE;
}

/* Opens `path` as fid `f`; the stat goes into the reply. */
static int fid_open(struct fid *f, uint32_t id, const char *path, struct zmsg *reply)
{
	struct file *rd;
	int rc = vfs_open(path, OREAD, &rd);
	if (rc) {
		return rc;
	}
	f->used = true;
	f->id = id;
	strlcpy(f->path, path, sizeof(f->path));
	f->rd = rd;
	f->wr = 0;
	zmsg_putstat(reply, vfs_type(rd), vfs_size(rd), vfs_name(rd));
	return 0;
}

/* One path element: never "." or "..", which could climb out of the
 * export, nor a '/'. Empty means the same file (a clone). */
static bool valid_name(const char *name)
{
	for (const char *c = name; *c; c++) {
		if (*c == '/') {
			return false;
		}
	}
	return strcmp(name, ".") && strcmp(name, "..");
}

static int do_walk(struct session *s, struct zmsg *req, struct zmsg *reply)
{
	uint32_t id = zmsg_get32(req), new_id = zmsg_get32(req);
	char name[VFS_NAME_MAX + 1], path[VFS_PATH_MAX + 1];
	zmsg_getstr(req, name, sizeof(name));
	if (req->bad) {
		return -EPROTO;
	}
	struct fid *from = find_fid(s, id), *to;
	if (!from) {
		return -EBADF;
	}
	if (!valid_name(name)) {
		return -EINVAL;
	}
	int rc = new_fid(s, new_id, &to);
	if (rc) {
		return rc;
	}
	if (!name[0]) {
		strlcpy(path, from->path, sizeof(path));
	} else if (ksnprintf(path, sizeof(path), "%s/%s", from->path, name) >= (int)sizeof(path)) {
		return -ENAMETOOLONG;
	}
	return fid_open(to, new_id, path, reply);
}

static int do_read(struct session *s, struct zmsg *req, struct zmsg *reply)
{
	uint32_t id = zmsg_get32(req), offset = zmsg_get32(req), count = zmsg_get32(req);
	if (req->bad) {
		return -EPROTO;
	}
	struct fid *f = find_fid(s, id);
	if (!f) {
		return -EBADF;
	}
	size_t room = s->msize - ZRP_READ_OVERHEAD;
	if (count > room) {
		count = (uint32_t)room;
	}
	size_t count_at = reply->pos;
	zmsg_put32(reply, 0);
	uint32_t n = 0;
	if (vfs_type(f->rd) == VNODE_DIR) {
		/* offset counts entries; whole stat records only. */
		for (uint32_t index = offset;; index++) {
			struct dirent d;
			int rc = vfs_readdir_at(f->rd, index, &d);
			if (rc < 0 && n == 0) {
				return rc;
			}
			if (rc <= 0) {
				break;
			}
			size_t record = 1 + 4 + 2 + strlen(d.name);
			if (n + record > count) {
				break;
			}
			zmsg_putstat(reply, d.type, d.size, d.name);
			n += (uint32_t)record;
		}
	} else {
		long got = vfs_pread(f->rd, reply->buf + reply->pos, count, offset);
		if (got < 0) {
			return (int)got;
		}
		reply->pos += (size_t)got;
		n = (uint32_t)got;
	}
	for (int i = 0; i < 4; i++) {
		reply->buf[count_at + i] = (uint8_t)(n >> (8 * i));
	}
	return 0;
}

static int do_write(struct session *s, struct zmsg *req, struct zmsg *reply)
{
	uint32_t id = zmsg_get32(req), offset = zmsg_get32(req), count = zmsg_get32(req);
	const uint8_t *data = zmsg_getbytes(req, count);
	if (req->bad || req->pos != req->cap) {
		return -EPROTO;
	}
	struct fid *f = find_fid(s, id);
	if (!f) {
		return -EBADF;
	}
	if (!f->wr) {
		int rc = vfs_open(f->path, OWRITE, &f->wr);
		if (rc) {
			f->wr = 0;
			return rc;
		}
	}
	long n = vfs_pwrite(f->wr, data, count, offset);
	if (n < 0) {
		return (int)n;
	}
	zmsg_put32(reply, (uint32_t)n);
	return 0;
}

/* Performs one request (not a Tversion); fills the reply after its
 * header, or returns an error for an Rerror. */
static int perform(struct zrp_server *srv, struct session *s, uint8_t type, struct zmsg *req,
                   struct zmsg *reply)
{
	switch (type) {
	case ZRP_TATTACH: {
		uint32_t id = zmsg_get32(req);
		char aname[VFS_NAME_MAX + 1];
		zmsg_getstr(req, aname, sizeof(aname));
		struct fid *f;
		if (req->bad) {
			return -EPROTO;
		}
		if (aname[0]) {
			return -ENOENT; /* one export, the default */
		}
		int rc = new_fid(s, id, &f);
		return rc ? rc : fid_open(f, id, srv->root, reply);
	}
	case ZRP_TWALK:
		return do_walk(s, req, reply);
	case ZRP_TREAD:
		return do_read(s, req, reply);
	case ZRP_TWRITE:
		return do_write(s, req, reply);
	case ZRP_TCLUNK:
	case ZRP_TSTAT: {
		struct fid *f = find_fid(s, zmsg_get32(req));
		if (req->bad) {
			return -EPROTO;
		}
		if (!f) {
			return -EBADF;
		}
		if (type == ZRP_TCLUNK) {
			clunk(f);
		} else {
			zmsg_putstat(reply, vfs_type(f->rd), vfs_size(f->rd), vfs_name(f->rd));
		}
		return 0;
	}
	default:
		return -EPROTO;
	}
}

static struct session *find_session(struct zrp_server *srv, uint32_t ip, uint16_t port)
{
	for (int i = 0; i < SESSIONS; i++) {
		struct session *s = &srv->sessions[i];
		if (s->used && s->ip == ip && s->port == port) {
			return s;
		}
	}
	return 0;
}

/* A new session, replacing the least recently active one if all are
 * taken. NULL when out of memory. */
static struct session *open_session(struct zrp_server *srv, uint32_t ip, uint16_t port)
{
	struct session *s = &srv->sessions[0];
	for (int i = 0; i < SESSIONS; i++) {
		struct session *c = &srv->sessions[i];
		if (!c->used || (s->used && c->last_active < s->last_active)) {
			s = c;
			if (!c->used) {
				break;
			}
		}
	}
	if (s->used) {
		session_reset(s);
	} else {
		s->last_request = kmalloc(ZRP_MSIZE);
		s->last_reply = kmalloc(ZRP_MSIZE);
		if (!s->last_request || !s->last_reply) {
			kfree(s->last_request);
			kfree(s->last_reply);
			s->last_request = s->last_reply = 0;
			return 0;
		}
		s->used = true;
	}
	s->ip = ip;
	s->port = port;
	return s;
}

static void send_reply(struct zrp_server *srv, uint32_t ip, uint16_t port, size_t len)
{
	if (len) {
		udp_send(srv->ep, ip, port, srv->reply, len);
	}
}

static void handle(struct zrp_server *srv, uint32_t ip, uint16_t port, size_t len)
{
	struct zmsg req, reply;
	zmsg_open(&req, srv->request, len);
	uint32_t size = zmsg_get32(&req);
	uint8_t type = zmsg_get8(&req);
	uint16_t tag = zmsg_get16(&req);
	if (req.bad || size != len) {
		srv->stats.errors++;
		return; /* not a ZRP message: no reply */
	}
	srv->stats.requests++;

	struct session *s = find_session(srv, ip, port);
	if (s && s->last_request_len == len && !memcmp(s->last_request, srv->request, len)) {
		srv->stats.duplicates++;
		s->last_active = timer_ticks();
		memcpy(srv->reply, s->last_reply, s->last_reply_len);
		send_reply(srv, ip, port, s->last_reply_len);
		return;
	}

	int rc;
	if (type == ZRP_TVERSION) {
		uint32_t msize = zmsg_get32(&req);
		char version[16];
		zmsg_getstr(&req, version, sizeof(version));
		rc = req.bad ? -EPROTO : strcmp(version, ZRP_VERSION) || msize < ZRP_MSIZE_MIN ? -EPROTO : 0;
		if (rc == 0) {
			s = s ? s : open_session(srv, ip, port);
			rc = s ? 0 : -ENOMEM;
		}
		if (rc == 0) {
			session_reset(s); /* a new version starts the session over */
			s->msize = msize < ZRP_MSIZE ? msize : ZRP_MSIZE;
			zmsg_start(&reply, srv->reply, s->msize, ZRP_RVERSION, tag);
			zmsg_put32(&reply, s->msize);
			zmsg_putstr(&reply, ZRP_VERSION);
		}
	} else if (!s) {
		rc = -EPROTO; /* Tversion first */
	} else {
		zmsg_start(&reply, srv->reply, s->msize, type + 1, tag);
		rc = perform(srv, s, type, &req, &reply);
	}
	if (rc) {
		srv->stats.errors++;
		zmsg_start(&reply, srv->reply, ZRP_MSIZE, ZRP_RERROR, tag);
		zmsg_put16(&reply, (uint16_t)-rc);
	}
	size_t reply_len = zmsg_finish(&reply);
	if (s) {
		s->last_active = timer_ticks();
		memcpy(s->last_request, srv->request, len);
		s->last_request_len = len;
		memcpy(s->last_reply, srv->reply, reply_len);
		s->last_reply_len = reply_len;
	}
	send_reply(srv, ip, port, reply_len);
}

static void server_main(void *arg)
{
	struct zrp_server *srv = arg;
	thread_set_namespace(srv->ns);
	while (!srv->stop) {
		uint32_t ip;
		uint16_t port;
		long n = udp_recv(srv->ep, srv->request, sizeof(srv->request), &ip, &port, RECV_TIMEOUT_MS);
		if (n > 0) {
			handle(srv, ip, port, (size_t)n);
		}
	}
	for (int i = 0; i < SESSIONS; i++) {
		struct session *s = &srv->sessions[i];
		if (s->used) {
			session_reset(s);
			kfree(s->last_request);
			kfree(s->last_reply);
		}
	}
	udp_close(srv->ep);
	uint32_t flags = cpu_irq_save();
	srv->stopped = true;
	waitq_wake_all(&srv->done);
	cpu_irq_restore(flags);
}

struct zrp_server *zrp_serve(const char *root, uint16_t port, int *err)
{
	struct zrp_server *srv = kmalloc(sizeof(*srv));
	if (!srv) {
		*err = -ENOMEM;
		return 0;
	}
	memset(srv, 0, sizeof(*srv));
	srv->done = (struct waitq)WAITQ_INIT;
	struct file *f;
	int rc = vfs_clean_path(root, srv->root);
	if (rc == 0 && (rc = vfs_open(srv->root, OREAD, &f)) == 0) {
		rc = vfs_type(f) == VNODE_DIR ? 0 : -ENOTDIR;
		vfs_close(f);
	}
	if (rc == 0) {
		srv->ep = udp_open(port, &rc);
	}
	if (rc) {
		kfree(srv);
		*err = rc;
		return 0;
	}
	srv->ns = thread_namespace();
	ns_ref(srv->ns);
	if (!thread_create("zrpd", server_main, srv)) {
		udp_close(srv->ep);
		ns_unref(srv->ns);
		kfree(srv);
		*err = -ENOMEM;
		return 0;
	}
	return srv;
}

void zrp_server_stop(struct zrp_server *srv)
{
	srv->stop = true;
	uint32_t flags = cpu_irq_save();
	while (!srv->stopped) {
		waitq_sleep(&srv->done);
	}
	cpu_irq_restore(flags);
	ns_unref(srv->ns);
	kfree(srv);
}

void zrp_server_stats(struct zrp_server *srv, struct zrp_server_stats *out)
{
	*out = srv->stats;
	out->sessions = out->fids = 0;
	for (int i = 0; i < SESSIONS; i++) {
		struct session *s = &srv->sessions[i];
		out->sessions += s->used;
		for (int k = 0; s->used && k < FIDS; k++) {
			out->fids += s->fids[k].used;
		}
	}
}
