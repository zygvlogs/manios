/* The ZRP server (docs/zrp.md): serves exported directory trees over
 * UDP.
 *
 * One thread receives datagrams and answers at once what needs no file
 * access -- Tversion, Tauth, and repeats -- and queues the rest for
 * worker threads (up to WORKERS_MAX, started as needed). So a request
 * that blocks, such as a read of a console waiting for a line or of a
 * window waiting for an event, holds up no other request (M13).
 *
 * A client is identified by its address and port: a session. It
 * retransmits a request until it is answered, with the same tag and
 * bytes, so:
 *  - a repeat of a request still being worked on is answered Rpending,
 *    which tells the client to keep waiting;
 *  - a repeat of one answered recently gets the same reply again, from
 *    the last DONE_CACHE replies kept per session; it is not performed
 *    a second time (a repeated walk or clunk must not fail).
 *
 * Exports are named (the attach name): the default one, "", from
 * export= at boot, and any a process makes of its own namespace with
 * SYS_EXPORT. Each fid remembers its export, whose namespace resolves
 * its walks. With a key, an attach must carry proof that the client
 * knows it (docs/zrp.md "Authentication"). */
#include "zrp.h"
#include "cpu.h"
#include "device.h"
#include "heap.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "mutex.h"
#include "namespace.h"
#include "net.h"
#include "sched.h"
#include "sha256.h"
#include "timer.h"
#include "udp.h"
#include "zkt_abi.h"

#define SESSIONS 16
#define FIDS 64
#define DONE_CACHE 8
#define WORKERS_MAX 16
#define QUEUE_MAX 64
#define RECV_TIMEOUT_MS 200 /* how often the receiver checks for stop */

struct export {
	struct export *next;
	int refs;     /* the list's, and one per fid on it */
	bool listed;
	char aname[VFS_NAME_MAX + 1];
	char root[VFS_PATH_MAX + 1];
	struct namespace *ns;
	const void *owner;
};

struct sfid {
	int refs;     /* the session table's, and one per request using it */
	uint32_t id;
	struct export *ex;
	char path[VFS_PATH_MAX + 1];
	struct file *file; /* opened for reading and writing if the file allows */
	bool writable;
};

/* A reply kept for a repeat of its request. */
struct done {
	uint16_t tag;
	uint32_t hash, request_len;
	uint8_t *reply;
	size_t reply_len;
};

struct session {
	bool used;
	uint32_t ip;
	uint16_t port;
	uint64_t last_active;
	uint32_t msize;
	uint32_t generation; /* a Tversion starts a new one */
	int jobs;            /* queued or being worked on */
	struct sfid *fids[FIDS];
	struct done done[DONE_CACHE];
	unsigned done_next;
	bool challenged;     /* Tauth seen: the nonces are set */
	uint8_t cnonce[ZRP_NONCE], snonce[ZRP_NONCE];
};

struct job {
	struct job *next;
	struct session *s;
	uint32_t generation, ip;
	uint16_t port, tag;
	uint32_t hash;
	size_t len;
	uint8_t msg[];
};

struct zrp_server {
	struct mutex lock; /* everything below except what is noted */
	struct udp_endpoint *ep;
	bool has_key;
	uint8_t key[ZRP_KEY];
	struct export *exports;
	struct session sessions[SESSIONS];
	struct zrp_server_stats stats;
	struct job *queue, *queue_tail, *active;
	int queued, workers, idle;
	volatile int started;  /* workers that have set themselves up */
	struct waitq work;   /* idle workers */
	struct waitq exited; /* stop waits here for the threads */
	volatile bool stop;
	volatile int threads;
	uint8_t rx[ZRP_MSIZE], tx[ZRP_MSIZE]; /* the receiver's */
};

static struct zrp_server *main_server;

/* FNV-1a: tells a repeat of a request from a new one with the same tag. */
static uint32_t hash(const uint8_t *p, size_t len)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < len; i++) {
		h = (h ^ p[i]) * 16777619u;
	}
	return h;
}

/* --- exports and fids (references change under srv->lock; what they
 * free is freed outside it, because closing a file may block) --- */

static void export_put(struct export *ex)
{
	if (ex && --ex->refs == 0) {
		ex->refs = -1; /* marks it for freeing by the caller */
	}
}

static void export_free_if_done(struct export *ex)
{
	if (ex && ex->refs == -1) {
		ns_unref(ex->ns);
		kfree(ex);
	}
}

/* Drops one reference; returns the fid if it must now be freed. */
static struct sfid *fid_put(struct sfid *f)
{
	if (f && --f->refs == 0) {
		export_put(f->ex);
		return f;
	}
	return 0;
}

static void fid_free(struct sfid *f)
{
	if (f) {
		vfs_close(f->file);
		export_free_if_done(f->ex);
		kfree(f);
	}
}

static struct sfid *find_fid(struct session *s, uint32_t id)
{
	for (int i = 0; i < FIDS; i++) {
		if (s->fids[i] && s->fids[i]->id == id) {
			return s->fids[i];
		}
	}
	return 0;
}

/* Under the lock: takes a reference to fid `id` of the job's session,
 * if the session is still the one the job was sent in. */
static struct sfid *get_fid(struct job *job, uint32_t id)
{
	struct sfid *f = job->s->generation == job->generation ? find_fid(job->s, id) : 0;
	if (f) {
		f->refs++;
	}
	return f;
}

/* Takes every fid out of a session; returns how many went into `out`
 * (FIDS entries) to be freed after unlocking. */
static int detach_fids(struct session *s, struct sfid **out)
{
	int n = 0;
	for (int i = 0; i < FIDS; i++) {
		struct sfid *f = s->fids[i] ? fid_put(s->fids[i]) : 0;
		s->fids[i] = 0;
		if (f) {
			out[n++] = f;
		}
	}
	return n;
}

static void clear_done(struct session *s)
{
	for (int i = 0; i < DONE_CACHE; i++) {
		kfree(s->done[i].reply);
		s->done[i].reply = 0;
	}
}

/* Opens `path` in the export's namespace, read-write if the file allows
 * it: a clone file (/dev/wsys/new) must see its write and its read on
 * one open file. */
static int open_fid(struct export *ex, const char *path, uint32_t id, struct sfid **out)
{
	struct sfid *f = kmalloc(sizeof(*f));
	if (!f) {
		return -ENOMEM;
	}
	thread_set_namespace(ex->ns);
	int rc = vfs_open(path, ORDWR, &f->file);
	f->writable = rc == 0;
	if (rc == -EROFS || rc == -EISDIR) {
		rc = vfs_open(path, OREAD, &f->file);
	}
	if (rc) {
		kfree(f);
		return rc;
	}
	f->refs = 1;
	f->id = id;
	f->ex = ex;
	strlcpy(f->path, path, sizeof(f->path));
	*out = f;
	return 0;
}

/* Under the lock: puts a new fid in the job's session table (with the
 * table's reference, and one on its export). */
static int insert_fid(struct job *job, struct sfid *f)
{
	struct session *s = job->s;
	if (s->generation != job->generation) {
		return -EIO; /* the session started over meanwhile */
	}
	if (find_fid(s, f->id)) {
		return -EINVAL;
	}
	for (int i = 0; i < FIDS; i++) {
		if (!s->fids[i]) {
			s->fids[i] = f;
			f->ex->refs++;
			return 0;
		}
	}
	return -EMFILE;
}

static void put_stat(struct zmsg *reply, struct sfid *f)
{
	zmsg_putstat(reply, vfs_type(f->file), vfs_size(f->file), vfs_name(f->file));
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

/* --- requests (worker threads) --- */

static int do_attach(struct zrp_server *srv, struct job *job, struct zmsg *req, struct zmsg *reply)
{
	uint32_t id = zmsg_get32(req);
	char aname[VFS_NAME_MAX + 1];
	zmsg_getstr(req, aname, sizeof(aname));
	size_t left = req->bad ? 0 : req->cap - req->pos;
	const uint8_t *mac = left == ZRP_MAC ? zmsg_getbytes(req, ZRP_MAC) : 0;
	if (req->bad || (left && !mac)) {
		return -EPROTO;
	}
	mutex_lock(&srv->lock);
	struct session *s = job->s;
	int rc = 0;
	if (srv->has_key) {
		uint8_t want[ZRP_MAC];
		if (!mac || !s->challenged) {
			rc = -EACCES;
		} else {
			zrp_auth_mac(srv->key, "client", s->cnonce, s->snonce, want);
			rc = equal_secret(mac, want, ZRP_MAC) ? 0 : -EACCES;
		}
	}
	struct export *ex = srv->exports;
	while (rc == 0 && ex && strcmp(ex->aname, aname)) {
		ex = ex->next;
	}
	if (rc == 0 && !ex) {
		rc = -ENOENT;
	}
	if (rc == 0) {
		ex->refs++; /* while opening: it may be unexported meanwhile */
	}
	mutex_unlock(&srv->lock);
	if (rc) {
		return rc;
	}
	struct sfid *f = 0;
	rc = open_fid(ex, ex->root, id, &f);
	mutex_lock(&srv->lock);
	if (rc == 0 && (rc = insert_fid(job, f)) == 0) {
		put_stat(reply, f);
	}
	export_put(ex);
	mutex_unlock(&srv->lock);
	if (rc && f) {
		vfs_close(f->file); /* opened, but not wanted after all */
		kfree(f);
	}
	export_free_if_done(ex);
	return rc;
}

static int do_walk(struct zrp_server *srv, struct job *job, struct zmsg *req, struct zmsg *reply)
{
	uint32_t id = zmsg_get32(req), new_id = zmsg_get32(req);
	char name[VFS_NAME_MAX + 1], path[VFS_PATH_MAX + 1];
	zmsg_getstr(req, name, sizeof(name));
	if (req->bad) {
		return -EPROTO;
	}
	if (!valid_name(name)) {
		return -EINVAL;
	}
	mutex_lock(&srv->lock);
	struct sfid *from = get_fid(job, id);
	int rc = !from ? -EBADF : find_fid(job->s, new_id) ? -EINVAL : 0;
	mutex_unlock(&srv->lock);
	struct sfid *to = 0;
	if (rc == 0) {
		if (!name[0]) {
			strlcpy(path, from->path, sizeof(path));
		} else if (ksnprintf(path, sizeof(path), "%s/%s", from->path, name) >= (int)sizeof(path)) {
			rc = -ENAMETOOLONG;
		}
		if (rc == 0) {
			rc = open_fid(from->ex, path, new_id, &to); /* from's reference keeps ex */
		}
	}
	mutex_lock(&srv->lock);
	if (rc == 0 && (rc = insert_fid(job, to)) == 0) {
		put_stat(reply, to);
	}
	struct sfid *gone = fid_put(from);
	mutex_unlock(&srv->lock);
	if (rc && to) {
		vfs_close(to->file);
		kfree(to);
	}
	fid_free(gone);
	return rc;
}

static int do_read(struct zrp_server *srv, struct job *job, struct zmsg *req, struct zmsg *reply)
{
	uint32_t id = zmsg_get32(req), offset = zmsg_get32(req), count = zmsg_get32(req);
	if (req->bad) {
		return -EPROTO;
	}
	mutex_lock(&srv->lock);
	struct sfid *f = get_fid(job, id);
	uint32_t msize = job->s->msize;
	mutex_unlock(&srv->lock);
	if (!f) {
		return -EBADF;
	}
	size_t room = msize - ZRP_READ_OVERHEAD;
	if (count > room) {
		count = (uint32_t)room;
	}
	size_t count_at = reply->pos;
	zmsg_put32(reply, 0);
	uint32_t n = 0;
	int rc = 0;
	thread_set_namespace(f->ex->ns);
	if (vfs_type(f->file) == VNODE_DIR) {
		/* offset counts entries; whole stat records only. */
		for (uint32_t index = offset;; index++) {
			struct dirent d;
			int got = vfs_readdir_at(f->file, index, &d);
			if (got < 0 && n == 0) {
				rc = got;
			}
			if (got <= 0) {
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
		long got = vfs_pread(f->file, reply->buf + reply->pos, count, offset);
		if (got < 0) {
			rc = (int)got;
		} else {
			reply->pos += (size_t)got;
			n = (uint32_t)got;
		}
	}
	for (int i = 0; i < 4; i++) {
		reply->buf[count_at + i] = (uint8_t)(n >> (8 * i));
	}
	mutex_lock(&srv->lock);
	struct sfid *gone = fid_put(f);
	mutex_unlock(&srv->lock);
	fid_free(gone);
	return rc;
}

static int do_write(struct zrp_server *srv, struct job *job, struct zmsg *req, struct zmsg *reply)
{
	uint32_t id = zmsg_get32(req), offset = zmsg_get32(req), count = zmsg_get32(req);
	const uint8_t *data = zmsg_getbytes(req, count);
	if (req->bad || req->pos != req->cap) {
		return -EPROTO;
	}
	mutex_lock(&srv->lock);
	struct sfid *f = get_fid(job, id);
	mutex_unlock(&srv->lock);
	if (!f) {
		return -EBADF;
	}
	long n = !f->writable ? (vfs_type(f->file) == VNODE_DIR ? -EISDIR : -EROFS)
	                      : vfs_pwrite(f->file, data, count, offset);
	if (n >= 0) {
		zmsg_put32(reply, (uint32_t)n);
	}
	mutex_lock(&srv->lock);
	struct sfid *gone = fid_put(f);
	mutex_unlock(&srv->lock);
	fid_free(gone);
	return n < 0 ? (int)n : 0;
}

static int do_clunk_or_stat(struct zrp_server *srv, struct job *job, uint8_t type, struct zmsg *req,
                            struct zmsg *reply)
{
	uint32_t id = zmsg_get32(req);
	if (req->bad) {
		return -EPROTO;
	}
	mutex_lock(&srv->lock);
	struct sfid *f = get_fid(job, id), *gone = 0;
	if (f && type == ZRP_TSTAT) {
		put_stat(reply, f);
	}
	if (f && type == ZRP_TCLUNK) {
		struct session *s = job->s;
		for (int i = 0; i < FIDS; i++) {
			if (s->fids[i] == f) {
				s->fids[i] = 0;
				fid_put(f); /* the table's */
			}
		}
	}
	if (f) {
		gone = fid_put(f);
	}
	mutex_unlock(&srv->lock);
	fid_free(gone);
	return f ? 0 : -EBADF;
}

static int perform(struct zrp_server *srv, struct job *job, uint8_t type, struct zmsg *req,
                   struct zmsg *reply)
{
	switch (type) {
	case ZRP_TATTACH:
		return do_attach(srv, job, req, reply);
	case ZRP_TWALK:
		return do_walk(srv, job, req, reply);
	case ZRP_TREAD:
		return do_read(srv, job, req, reply);
	case ZRP_TWRITE:
		return do_write(srv, job, req, reply);
	case ZRP_TCLUNK:
	case ZRP_TSTAT:
		return do_clunk_or_stat(srv, job, type, req, reply);
	default:
		return -EPROTO;
	}
}

/* Under the lock: keeps a reply for repeats of its request. */
static void remember(struct session *s, uint16_t tag, uint32_t h, size_t request_len,
                     const uint8_t *reply, size_t len)
{
	uint8_t *copy = kmalloc(len);
	if (!copy) {
		return;
	}
	memcpy(copy, reply, len);
	struct done *d = &s->done[s->done_next++ % DONE_CACHE];
	kfree(d->reply);
	*d = (struct done){ tag, h, (uint32_t)request_len, copy, len };
}

static void unlink_active(struct zrp_server *srv, struct job *job)
{
	struct job **link = &srv->active;
	while (*link != job) {
		link = &(*link)->next;
	}
	*link = job->next;
}

static void work(struct zrp_server *srv, struct job *job, uint8_t *buf)
{
	struct zmsg req, reply;
	zmsg_open(&req, job->msg, job->len);
	req.pos = ZRP_HEADER;
	uint8_t type = job->msg[4];
	mutex_lock(&srv->lock);
	uint32_t msize = job->s->msize;
	mutex_unlock(&srv->lock);
	zmsg_start(&reply, buf, msize, type + 1, job->tag);
	int rc = perform(srv, job, type, &req, &reply);
	if (rc) {
		zmsg_start(&reply, buf, ZRP_MSIZE, ZRP_RERROR, job->tag);
		zmsg_put16(&reply, (uint16_t)-rc);
	}
	size_t len = zmsg_finish(&reply);
	mutex_lock(&srv->lock);
	struct session *s = job->s;
	bool current = s->generation == job->generation;
	if (current) {
		if (rc) {
			srv->stats.errors++;
		}
		s->last_active = timer_ticks();
		remember(s, job->tag, job->hash, job->len, buf, len);
	}
	s->jobs--;
	unlink_active(srv, job);
	/* UDP has no hang-up: a session a clunk leaves without fids is over
	 * (a repeat of that clunk then gets EPROTO, which a client ignores).
	 * After a failed attach the session stays, for another try, but its
	 * kept replies go: repeating a failed attach does no harm. */
	if (current && !s->jobs && (type == ZRP_TCLUNK || (type == ZRP_TATTACH && rc))) {
		bool empty = true;
		for (int i = 0; i < FIDS && empty; i++) {
			empty = !s->fids[i];
		}
		if (empty) {
			clear_done(s);
			if (type == ZRP_TCLUNK) {
				s->used = false;
				s->generation++;
			}
		}
	}
	mutex_unlock(&srv->lock);
	if (current && len) {
		udp_send(srv->ep, job->ip, job->port, buf, len);
	}
	kfree(job);
}

static void thread_done(struct zrp_server *srv)
{
	uint32_t flags = cpu_irq_save();
	srv->threads--;
	waitq_wake_all(&srv->exited);
	cpu_irq_restore(flags);
}

static void worker_main(void *arg)
{
	struct zrp_server *srv = arg;
	uint8_t *buf = kmalloc(ZRP_MSIZE);
	/* Requests switch to their export's namespace; between them the
	 * worker goes home, so it keeps no namespace alive. */
	struct namespace *home = thread_namespace();
	ns_ref(home);
	mutex_lock(&srv->lock);
	srv->started++;
	srv->idle--; /* counted idle since it was started (enough_workers) */
	while (buf) {
		while (!srv->queue && !srv->stop) {
			if (srv->idle) {
				goto leave; /* one idle worker is enough */
			}
			srv->idle++;
			mutex_unlock(&srv->lock);
			/* The receiver queues under the lock, then wakes us with
			 * interrupts off: checking again with them off can't miss it. */
			uint32_t flags = cpu_irq_save();
			if (!srv->queue && !srv->stop) {
				waitq_sleep(&srv->work);
			}
			cpu_irq_restore(flags);
			mutex_lock(&srv->lock);
			srv->idle--;
		}
		struct job *job = srv->queue;
		if (!job) {
			break; /* stopping */
		}
		srv->queue = job->next;
		if (!srv->queue) {
			srv->queue_tail = 0;
		}
		srv->queued--;
		job->next = srv->active;
		srv->active = job;
		mutex_unlock(&srv->lock);
		work(srv, job, buf);
		thread_set_namespace(home);
		mutex_lock(&srv->lock);
	}
leave:
	srv->workers--;
	srv->stats.workers = (uint32_t)srv->workers;
	mutex_unlock(&srv->lock);
	ns_unref(home);
	kfree(buf);
	thread_done(srv);
}

/* Under the lock: more workers while jobs outnumber the idle ones. */
static void enough_workers(struct zrp_server *srv)
{
	while (srv->queued > srv->idle && srv->workers < WORKERS_MAX) {
		uint32_t flags = cpu_irq_save();
		srv->threads++;
		cpu_irq_restore(flags);
		if (!thread_create("zrpw", worker_main, srv)) {
			thread_done(srv);
			return;
		}
		srv->workers++;
		srv->idle++; /* until it starts: then it takes a job, or waits */
		srv->stats.workers = (uint32_t)srv->workers;
	}
}

/* --- the receiver --- */

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

/* Under the lock: a free session, or the least recently active one with
 * nothing in progress (its fids go into `gone`). NULL if all are busy. */
static struct session *open_session(struct zrp_server *srv, uint32_t ip, uint16_t port,
                                    struct sfid **gone, int *ngone)
{
	struct session *victim = 0;
	for (int i = 0; i < SESSIONS; i++) {
		struct session *c = &srv->sessions[i];
		if (!c->used) {
			victim = c;
			break;
		}
		if (!c->jobs && (!victim || c->last_active < victim->last_active)) {
			victim = c;
		}
	}
	if (!victim) {
		return 0;
	}
	if (victim->used) {
		*ngone = detach_fids(victim, gone);
		clear_done(victim);
	}
	uint32_t generation = victim->generation;
	memset(victim, 0, sizeof(*victim));
	victim->generation = generation + 1;
	victim->used = true;
	victim->ip = ip;
	victim->port = port;
	return victim;
}

/* Under the lock: Tversion, answered at once. Returns the reply length. */
static size_t version(struct zrp_server *srv, struct session **sp, uint32_t ip, uint16_t port,
                      uint16_t tag, struct zmsg *req, struct sfid **gone, int *ngone)
{
	struct zmsg reply;
	uint32_t msize = zmsg_get32(req);
	char name[16];
	zmsg_getstr(req, name, sizeof(name));
	int rc = req->bad || strcmp(name, ZRP_VERSION) || msize < ZRP_MSIZE_MIN ? -EPROTO : 0;
	struct session *s = *sp;
	if (rc == 0 && !s) {
		s = *sp = open_session(srv, ip, port, gone, ngone);
		rc = s ? 0 : -EBUSY;
	} else if (rc == 0) {
		/* A new version starts the session over: requests still being
		 * worked on will find they belong to the old one. */
		*ngone = detach_fids(s, gone);
		clear_done(s);
		s->generation++;
		s->challenged = false;
	}
	if (rc == 0) {
		s->msize = msize < ZRP_MSIZE ? msize : ZRP_MSIZE;
		zmsg_start(&reply, srv->tx, s->msize, ZRP_RVERSION, tag);
		zmsg_put32(&reply, s->msize);
		zmsg_putstr(&reply, ZRP_VERSION);
	} else {
		zmsg_start(&reply, srv->tx, ZRP_MSIZE, ZRP_RERROR, tag);
		zmsg_put16(&reply, (uint16_t)-rc);
	}
	return zmsg_finish(&reply);
}

/* Under the lock: Tauth, answered at once. */
static size_t auth(struct zrp_server *srv, struct session *s, uint16_t tag, struct zmsg *req)
{
	struct zmsg reply;
	const uint8_t *cnonce = zmsg_getbytes(req, ZRP_NONCE);
	int rc = req->bad || req->pos != req->cap ? -EPROTO : !srv->has_key ? -ENOSYS : 0;
	if (rc == 0) {
		memcpy(s->cnonce, cnonce, ZRP_NONCE);
		zrp_nonce(s->snonce);
		s->challenged = true;
		uint8_t mac[ZRP_MAC];
		zrp_auth_mac(srv->key, "server", s->cnonce, s->snonce, mac);
		zmsg_start(&reply, srv->tx, s->msize, ZRP_RAUTH, tag);
		zmsg_putbytes(&reply, s->snonce, ZRP_NONCE);
		zmsg_putbytes(&reply, mac, ZRP_MAC);
	} else {
		zmsg_start(&reply, srv->tx, ZRP_MSIZE, ZRP_RERROR, tag);
		zmsg_put16(&reply, (uint16_t)-rc);
	}
	return zmsg_finish(&reply);
}

static void receive(struct zrp_server *srv, uint32_t ip, uint16_t port, size_t len)
{
	struct zmsg req;
	zmsg_open(&req, srv->rx, len);
	uint32_t size = zmsg_get32(&req);
	uint8_t type = zmsg_get8(&req);
	uint16_t tag = zmsg_get16(&req);
	struct sfid *gone[FIDS];
	int ngone = 0;
	size_t reply_len = 0;

	mutex_lock(&srv->lock);
	if (req.bad || size != len) {
		srv->stats.errors++;
		mutex_unlock(&srv->lock);
		return; /* not a ZRP message: no reply */
	}
	srv->stats.requests++;
	uint32_t h = hash(srv->rx, len);
	struct session *s = find_session(srv, ip, port);
	bool answered = false;
	if (s) {
		/* A repeat of something in progress, or answered already? */
		for (int pass = 0; pass < 2 && !answered; pass++) {
			for (struct job *j = pass ? srv->active : srv->queue; j; j = j->next) {
				if (j->s == s && j->generation == s->generation && j->tag == tag && j->hash == h
				    && j->len == len) {
					struct zmsg reply;
					zmsg_start(&reply, srv->tx, ZRP_MSIZE, ZRP_RPENDING, tag);
					reply_len = zmsg_finish(&reply);
					srv->stats.pending++;
					answered = true;
					enough_workers(srv);
					break;
				}
			}
		}
		for (int i = 0; i < DONE_CACHE && !answered; i++) {
			struct done *d = &s->done[i];
			if (d->reply && d->tag == tag && d->hash == h && d->request_len == len) {
				memcpy(srv->tx, d->reply, d->reply_len);
				reply_len = d->reply_len;
				srv->stats.duplicates++;
				answered = true;
			}
		}
	}
	if (!answered) {
		struct zmsg reply;
		if (type == ZRP_TVERSION) {
			reply_len = version(srv, &s, ip, port, tag, &req, gone, &ngone);
		} else if (!s) {
			zmsg_start(&reply, srv->tx, ZRP_MSIZE, ZRP_RERROR, tag);
			zmsg_put16(&reply, EPROTO); /* Tversion first */
			reply_len = zmsg_finish(&reply);
		} else if (type == ZRP_TAUTH) {
			reply_len = auth(srv, s, tag, &req);
		} else if (srv->queued < QUEUE_MAX) {
			struct job *job = kmalloc(sizeof(*job) + len);
			if (job) {
				*job = (struct job){ 0, s, s->generation, ip, port, tag, h, len };
				memcpy(job->msg, srv->rx, len);
				if (srv->queue_tail) {
					srv->queue_tail->next = job;
				} else {
					srv->queue = job;
				}
				srv->queue_tail = job;
				srv->queued++;
				s->jobs++;
				enough_workers(srv);
				uint32_t flags = cpu_irq_save();
				waitq_wake_one(&srv->work);
				cpu_irq_restore(flags);
			}
		} /* else: dropped; the client will send it again */
		if (s && reply_len && (type == ZRP_TVERSION || type == ZRP_TAUTH)) {
			s->last_active = timer_ticks();
			remember(s, tag, h, len, srv->tx, reply_len);
		}
	}
	mutex_unlock(&srv->lock);
	for (int i = 0; i < ngone; i++) {
		fid_free(gone[i]);
	}
	if (reply_len) {
		udp_send(srv->ep, ip, port, srv->tx, reply_len);
	}
}

static void receiver_main(void *arg)
{
	struct zrp_server *srv = arg;
	while (!srv->stop) {
		uint32_t ip;
		uint16_t port;
		long n = udp_recv(srv->ep, srv->rx, sizeof(srv->rx), &ip, &port, RECV_TIMEOUT_MS);
		if (n > 0) {
			receive(srv, ip, port, (size_t)n);
		}
	}
	thread_done(srv);
}

/* --- the interface --- */

struct zrp_server *zrp_serve(uint16_t port, const uint8_t *key, int *err)
{
	struct zrp_server *srv = kmalloc(sizeof(*srv));
	if (!srv) {
		*err = -ENOMEM;
		return 0;
	}
	memset(srv, 0, sizeof(*srv));
	srv->lock = (struct mutex)MUTEX_INIT;
	srv->work = (struct waitq)WAITQ_INIT;
	srv->exited = (struct waitq)WAITQ_INIT;
	if (key) {
		srv->has_key = true;
		memcpy(srv->key, key, ZRP_KEY);
	}
	int rc;
	srv->ep = udp_open(port, &rc);
	if (!srv->ep) {
		kfree(srv);
		*err = rc;
		return 0;
	}
	srv->threads = 1;
	if (!thread_create("zrpd", receiver_main, srv)) {
		udp_close(srv->ep);
		kfree(srv);
		*err = -ENOMEM;
		return 0;
	}
	/* One worker waits from the start; more come when requests do. */
	mutex_lock(&srv->lock);
	srv->queued = 1;
	enough_workers(srv);
	srv->queued = 0;
	mutex_unlock(&srv->lock);
	/* ... and has its buffer before we return, so what it allocates
	 * isn't counted against whoever runs next (the boot self-tests). */
	for (int i = 0; i < 100 && !srv->started; i++) {
		thread_yield();
	}
	return srv;
}

static bool valid_aname(const char *aname)
{
	for (const char *c = aname; *c; c++) {
		if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9')
		      || *c == '.' || *c == '-' || *c == '_')) {
			return false;
		}
	}
	return strlen(aname) <= 31;
}

int zrp_export(struct zrp_server *srv, const char *aname, const char *path, const void *owner)
{
	if (!valid_aname(aname)) {
		return -EINVAL;
	}
	struct export *ex = kmalloc(sizeof(*ex));
	if (!ex) {
		return -ENOMEM;
	}
	memset(ex, 0, sizeof(*ex));
	struct file *f;
	int rc = vfs_clean_path(path, ex->root);
	if (rc == 0 && (rc = vfs_open(ex->root, OREAD, &f)) == 0) {
		rc = vfs_type(f) == VNODE_DIR ? 0 : -ENOTDIR;
		vfs_close(f);
	}
	if (rc) {
		kfree(ex);
		return rc;
	}
	strlcpy(ex->aname, aname, sizeof(ex->aname));
	ex->ns = thread_namespace();
	ns_ref(ex->ns);
	ex->owner = owner;
	ex->refs = 1;
	ex->listed = true;
	mutex_lock(&srv->lock);
	for (struct export *e = srv->exports; e; e = e->next) {
		if (!strcmp(e->aname, aname)) {
			rc = -EEXIST;
		}
	}
	if (rc == 0) {
		ex->next = srv->exports;
		srv->exports = ex;
	}
	mutex_unlock(&srv->lock);
	if (rc) {
		ns_unref(ex->ns);
		kfree(ex);
	}
	return rc;
}

/* Under the lock: takes exports matching aname (or any, if NULL) and
 * owner out of the list; returns those to be freed. */
static struct export *unlist(struct zrp_server *srv, const char *aname, const void *owner,
                             bool any_owner, int *found, int *refused)
{
	struct export *free_list = 0;
	for (struct export **link = &srv->exports; *link;) {
		struct export *e = *link;
		bool match = !aname || !strcmp(e->aname, aname);
		if (match && !any_owner && e->owner != owner) {
			(*refused)++;
			match = false;
		}
		if (!match) {
			link = &e->next;
			continue;
		}
		(*found)++;
		*link = e->next;
		e->listed = false;
		export_put(e);
		if (e->refs == -1) {
			e->next = free_list;
			free_list = e;
		}
	}
	return free_list;
}

static void free_exports(struct export *list)
{
	while (list) {
		struct export *next = list->next;
		export_free_if_done(list);
		list = next;
	}
}

int zrp_unexport(struct zrp_server *srv, const char *aname, const void *owner)
{
	int found = 0, refused = 0;
	mutex_lock(&srv->lock);
	struct export *list = unlist(srv, aname, owner, false, &found, &refused);
	mutex_unlock(&srv->lock);
	free_exports(list);
	return found ? 0 : refused ? -EPERM : -ENOENT;
}

void zrp_unexport_owner(struct zrp_server *srv, const void *owner)
{
	if (!srv) {
		return;
	}
	int found = 0, refused = 0;
	mutex_lock(&srv->lock);
	struct export *list = unlist(srv, 0, owner, false, &found, &refused);
	mutex_unlock(&srv->lock);
	free_exports(list);
}

void zrp_server_stop(struct zrp_server *srv)
{
	srv->stop = true;
	uint32_t flags = cpu_irq_save();
	waitq_wake_all(&srv->work);
	while (srv->threads) {
		waitq_sleep(&srv->exited);
	}
	cpu_irq_restore(flags);
	/* No thread is left: nothing else touches the server. */
	struct sfid *gone[FIDS];
	for (int i = 0; i < SESSIONS; i++) {
		struct session *s = &srv->sessions[i];
		if (s->used) {
			int n = detach_fids(s, gone);
			for (int k = 0; k < n; k++) {
				fid_free(gone[k]);
			}
			clear_done(s);
		}
	}
	while (srv->queue) {
		struct job *next = srv->queue->next;
		kfree(srv->queue);
		srv->queue = next;
	}
	int found = 0, refused = 0;
	free_exports(unlist(srv, 0, 0, true, &found, &refused));
	udp_close(srv->ep);
	kfree(srv);
}

void zrp_server_stats(struct zrp_server *srv, struct zrp_server_stats *out)
{
	mutex_lock(&srv->lock);
	*out = srv->stats;
	out->sessions = out->fids = 0;
	for (int i = 0; i < SESSIONS; i++) {
		struct session *s = &srv->sessions[i];
		out->sessions += s->used;
		for (int k = 0; s->used && k < FIDS; k++) {
			out->fids += s->fids[k] != 0;
		}
	}
	mutex_unlock(&srv->lock);
}

struct zrp_server *zrp_main_server(void)
{
	return main_server;
}

/* Device "zrp" (M13): the main server's state, as text --
 *   key set|none
 *   export NAME PATH      (one per export; "" is the default)
 *   sessions N fids N workers N
 *   requests N repeats N pending N errors N
 *   retransmits N         (by this machine's client)
 * It never shows the key itself. */
static long zrp_dev_read(struct device *dev, uint32_t offset, void *buf, size_t len)
{
	(void)dev;
	static char text[1024];
	static struct mutex text_lock = MUTEX_INIT;
	struct zrp_server *srv = main_server;
	mutex_lock(&text_lock);
	size_t n = (size_t)ksnprintf(text, sizeof(text), "key %s\n", zrp_key() ? "set" : "none");
	if (srv) {
		struct zrp_server_stats st;
		zrp_server_stats(srv, &st);
		mutex_lock(&srv->lock);
		for (struct export *e = srv->exports; e && n < sizeof(text) - 300; e = e->next) {
			n += (size_t)ksnprintf(text + n, sizeof(text) - n, "export \"%s\" %s\n", e->aname, e->root);
		}
		mutex_unlock(&srv->lock);
		n += (size_t)ksnprintf(text + n, sizeof(text) - n,
		                       "sessions %lu fids %lu workers %lu\n"
		                       "requests %lu repeats %lu pending %lu errors %lu\n",
		                       (unsigned long)st.sessions, (unsigned long)st.fids,
		                       (unsigned long)st.workers, (unsigned long)st.requests,
		                       (unsigned long)st.duplicates, (unsigned long)st.pending,
		                       (unsigned long)st.errors);
	}
	n += (size_t)ksnprintf(text + n, sizeof(text) - n, "retransmits %lu\n",
	                       (unsigned long)zrp_client_retransmits());
	long got = 0;
	if (offset < n) {
		got = (long)(len < n - offset ? len : n - offset);
		memcpy(buf, text + offset, (size_t)got);
	}
	mutex_unlock(&text_lock);
	return got;
}

static const struct char_device_ops zrp_dev_ops = { .pread = zrp_dev_read };
static struct device zrp_device = { .name = "zrp", .class = DEVICE_CHAR, .char_ops = &zrp_dev_ops };

void zrp_start_main(void)
{
	int err;
	main_server = zrp_serve(ZRP_PORT, zrp_key(), &err);
	if (!main_server) {
		kprintf("zrp: cannot serve on udp port %d: %s\n", ZRP_PORT, kstrerror(err));
	}
	device_register(&zrp_device);
}
