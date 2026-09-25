/* Serving files from a program over ZRP (zrpsrv.h, docs/zrp.md). */
#include <zrpsrv.h>
#include <errno.h>
#include <manios.h>
#include <stdlib.h>
#include <string.h>

#define HEADER 7
#define TVERSION 1
#define TATTACH 3
#define TWALK 5
#define TREAD 7
#define TWRITE 9
#define TCLUNK 11
#define TSTAT 13
#define RERROR 64

/* --- message helpers (bounds-checked; a bad field marks the message) --- */

struct msg {
	uint8_t *p;
	uint32_t len, pos;
	bool bad;
};

static uint8_t *take(struct msg *m, uint32_t n)
{
	if (m->bad || n > m->len - m->pos) {
		m->bad = true;
		return NULL;
	}
	uint8_t *at = m->p + m->pos;
	m->pos += n;
	return at;
}

static uint32_t get(struct msg *m, int bytes)
{
	uint8_t *at = take(m, (uint32_t)bytes);
	uint32_t v = 0;
	for (int i = 0; at && i < bytes; i++) {
		v |= (uint32_t)at[i] << (8 * i);
	}
	return v;
}

static void get_str(struct msg *m, char *out, size_t size)
{
	uint32_t n = get(m, 2);
	uint8_t *at = take(m, n);
	if (!at || n >= size) {
		m->bad = true;
		out[0] = '\0';
		return;
	}
	memcpy(out, at, n);
	out[n] = '\0';
}

static void put(struct msg *m, uint32_t v, int bytes)
{
	uint8_t *at = take(m, (uint32_t)bytes);
	for (int i = 0; at && i < bytes; i++) {
		at[i] = (uint8_t)(v >> (8 * i));
	}
}

static void put_str(struct msg *m, const char *s)
{
	uint32_t n = (uint32_t)strlen(s);
	put(m, n, 2);
	uint8_t *at = take(m, n);
	if (at) {
		memcpy(at, s, n);
	}
}

static void put_stat(struct msg *m, const struct zsrv_node *n)
{
	put(m, n->type, 1);
	put(m, n->length, 4);
	put_str(m, n->name);
}

static uint32_t stat_size(const struct zsrv_node *n)
{
	return 1 + 4 + 2 + (uint32_t)strlen(n->name);
}

static void start(struct zsrv *s, struct msg *m, uint8_t type, uint16_t tag)
{
	m->p = s->out;
	m->len = s->msize;
	m->pos = 4;
	m->bad = false;
	put(m, type, 1);
	put(m, tag, 2);
}

static void finish(struct zsrv *s, struct msg *m)
{
	if (m->bad) {
		return;
	}
	for (int i = 0; i < 4; i++) {
		s->out[i] = (uint8_t)(m->pos >> (8 * i));
	}
	write(s->fd, s->out, m->pos);
}

static void send_error(struct zsrv *s, uint16_t tag, int err)
{
	struct msg m;
	start(s, &m, RERROR, tag);
	put(&m, (uint32_t)err, 2);
	finish(s, &m);
}

/* --- the tree and the fids --- */

struct zsrv *zsrv_new(int fd, const struct zsrv_ops *ops, void *aux)
{
	struct zsrv *s = calloc(1, sizeof(*s));
	if (!s) {
		return NULL;
	}
	s->in = malloc(ZSRV_MSIZE);
	s->out = malloc(ZSRV_MSIZE);
	if (!s->in || !s->out) {
		free(s->in);
		free(s->out);
		free(s);
		return NULL;
	}
	s->fd = fd;
	s->ops = ops;
	s->aux = aux;
	s->msize = ZSRV_MSIZE;
	strcpy(s->root.name, "/");
	s->root.type = ZKT_TYPE_DIR;
	s->root.refs = 1; /* never freed */
	return s;
}

struct zsrv_node *zsrv_add(struct zsrv_node *dir, const char *name, uint32_t type, void *aux)
{
	struct zsrv_node *n = calloc(1, sizeof(*n));
	if (!n) {
		return NULL;
	}
	strlcpy(n->name, name, sizeof(n->name));
	n->type = type;
	n->aux = aux;
	n->parent = dir;
	/* Appended, so listings keep the order of creation. */
	struct zsrv_node **link = &dir->children;
	while (*link) {
		link = &(*link)->next;
	}
	*link = n;
	return n;
}

static void node_put(struct zsrv_node *n)
{
	if (--n->refs == 0 && n->removed) {
		free(n);
	}
}

void zsrv_remove(struct zsrv *s, struct zsrv_node *node)
{
	while (node->children) {
		zsrv_remove(s, node->children);
	}
	struct zsrv_node **link = &node->parent->children;
	while (*link != node) {
		link = &(*link)->next;
	}
	*link = node->next;
	node->removed = true;
	/* Deferred reads on it will never have an answer. */
	for (struct zsrv_req **r = &s->deferred; *r;) {
		struct zsrv_req *req = *r;
		if (req->fid->node == node) {
			*r = req->next;
			send_error(s, req->tag, EIO);
			free(req);
		} else {
			r = &req->next;
		}
	}
	if (node->refs == 0) {
		free(node);
	}
}

static struct zsrv_fid *find_fid(struct zsrv *s, uint32_t id)
{
	struct zsrv_fid *f = s->fids;
	while (f && f->id != id) {
		f = f->next;
	}
	return f;
}

static int new_fid(struct zsrv *s, uint32_t id, struct zsrv_node *node)
{
	if (find_fid(s, id)) {
		return -EINVAL;
	}
	struct zsrv_fid *f = calloc(1, sizeof(*f));
	if (!f) {
		return -ENOMEM;
	}
	f->id = id;
	f->node = node;
	node->refs++;
	f->next = s->fids;
	s->fids = f;
	return 0;
}

static void clunk(struct zsrv *s, struct zsrv_fid *f)
{
	for (struct zsrv_req **r = &s->deferred; *r;) {
		struct zsrv_req *req = *r;
		if (req->fid == f) {
			*r = req->next;
			send_error(s, req->tag, EIO);
			free(req);
		} else {
			r = &req->next;
		}
	}
	if (s->ops->clunk) {
		s->ops->clunk(s, f);
	}
	struct zsrv_fid **link = &s->fids;
	while (*link != f) {
		link = &(*link)->next;
	}
	*link = f->next;
	node_put(f->node);
	free(f);
}

int zsrv_fid_count(const struct zsrv *s)
{
	int n = 0;
	for (const struct zsrv_fid *f = s->fids; f; f = f->next) {
		n++;
	}
	return n;
}

/* --- requests --- */

void zsrv_respond(struct zsrv *s, struct zsrv_req *req, const void *data, uint32_t len)
{
	for (struct zsrv_req **r = &s->deferred; *r; r = &(*r)->next) {
		if (*r == req) {
			*r = req->next;
			break;
		}
	}
	struct msg m;
	start(s, &m, TREAD + 1, req->tag);
	if (len > req->count) {
		len = req->count;
	}
	put(&m, len, 4);
	uint8_t *at = take(&m, len);
	if (at) {
		memcpy(at, data, len);
	}
	finish(s, &m);
	free(req);
}

void zsrv_respond_error(struct zsrv *s, struct zsrv_req *req, int err)
{
	for (struct zsrv_req **r = &s->deferred; *r; r = &(*r)->next) {
		if (*r == req) {
			*r = req->next;
			break;
		}
	}
	send_error(s, req->tag, err);
	free(req);
}

static int do_read(struct zsrv *s, struct zsrv_fid *f, struct msg *in, uint16_t tag)
{
	uint32_t offset = get(in, 4), count = get(in, 4);
	if (in->bad) {
		return -EPROTO;
	}
	if (count > s->msize - HEADER - 4) {
		count = s->msize - HEADER - 4;
	}
	struct msg m;
	start(s, &m, TREAD + 1, tag);
	uint32_t count_at = m.pos, n = 0;
	put(&m, 0, 4);
	if (f->node->type == ZKT_TYPE_DIR) {
		/* offset counts entries; whole stat records only */
		struct zsrv_node *c = f->node->children;
		for (uint32_t i = 0; c && i < offset; i++) {
			c = c->next;
		}
		for (; c && n + stat_size(c) <= count; c = c->next) {
			put_stat(&m, c);
			n += stat_size(c);
		}
	} else if (s->ops->read) {
		struct zsrv_req *req = calloc(1, sizeof(*req));
		if (!req) {
			return -ENOMEM;
		}
		req->tag = tag;
		req->fid = f;
		req->offset = offset;
		req->count = count;
		long got = s->ops->read(s, req, s->out + m.pos);
		if (got == ZSRV_DEFER) {
			req->next = s->deferred;
			s->deferred = req;
			return 0;
		}
		free(req);
		if (got < 0) {
			return (int)got;
		}
		n = (uint32_t)got > count ? count : (uint32_t)got;
		m.pos += n;
	}
	for (int i = 0; i < 4; i++) {
		s->out[count_at + i] = (uint8_t)(n >> (8 * i));
	}
	finish(s, &m);
	return 0;
}

static bool valid_name(const char *name)
{
	return !strchr(name, '/') && strcmp(name, ".") && strcmp(name, "..");
}

/* Answers one request; an error goes back as Rerror. */
static int perform(struct zsrv *s, uint8_t type, uint16_t tag, struct msg *in)
{
	struct msg m;
	char name[ZKT_NAME_MAX + 1];
	if (type == TVERSION) {
		uint32_t msize = get(in, 4);
		get_str(in, name, sizeof(name));
		if (in->bad || strcmp(name, "ZRP2") || msize < 256) {
			return -EPROTO;
		}
		while (s->fids) {
			clunk(s, s->fids);
		}
		s->msize = msize < ZSRV_MSIZE ? msize : ZSRV_MSIZE;
		start(s, &m, type + 1, tag);
		put(&m, s->msize, 4);
		put_str(&m, "ZRP2");
		finish(s, &m);
		return 0;
	}
	if (type == TATTACH) {
		uint32_t id = get(in, 4);
		get_str(in, name, sizeof(name));
		if (in->bad) {
			return -EPROTO;
		}
		if (name[0]) {
			return -ENOENT;
		}
		int rc = new_fid(s, id, &s->root);
		if (rc) {
			return rc;
		}
		start(s, &m, type + 1, tag);
		put_stat(&m, &s->root);
		finish(s, &m);
		return 0;
	}
	struct zsrv_fid *f = find_fid(s, get(in, 4));
	if (in->bad) {
		return -EPROTO;
	}
	if (!f) {
		return -EBADF;
	}
	switch (type) {
	case TWALK: {
		uint32_t new_id = get(in, 4);
		get_str(in, name, sizeof(name));
		if (in->bad) {
			return -EPROTO;
		}
		if (!valid_name(name)) {
			return -EINVAL;
		}
		struct zsrv_node *n = f->node;
		if (n->removed) {
			return -ENOENT;
		}
		if (name[0]) {
			if (n->type != ZKT_TYPE_DIR) {
				return -ENOTDIR;
			}
			for (n = n->children; n && strcmp(n->name, name); n = n->next) {
			}
			if (!n) {
				return -ENOENT;
			}
		}
		int rc = new_fid(s, new_id, n);
		if (rc) {
			return rc;
		}
		start(s, &m, type + 1, tag);
		put_stat(&m, n);
		finish(s, &m);
		return 0;
	}
	case TREAD:
		return f->node->removed ? -EIO : do_read(s, f, in, tag);
	case TWRITE: {
		uint32_t offset = get(in, 4), count = get(in, 4);
		uint8_t *data = take(in, count);
		if (in->bad || in->pos != in->len) {
			return -EPROTO;
		}
		if (f->node->removed) {
			return -EIO;
		}
		if (f->node->type == ZKT_TYPE_DIR) {
			return -EISDIR;
		}
		long n = s->ops->write ? s->ops->write(s, f, offset, data, count) : -EROFS;
		if (n < 0) {
			return (int)n;
		}
		start(s, &m, type + 1, tag);
		put(&m, (uint32_t)n, 4);
		finish(s, &m);
		return 0;
	}
	case TCLUNK:
		clunk(s, f);
		start(s, &m, type + 1, tag);
		finish(s, &m);
		return 0;
	case TSTAT:
		start(s, &m, type + 1, tag);
		put_stat(&m, f->node);
		finish(s, &m);
		return 0;
	default:
		return -EPROTO;
	}
}

int zsrv_handle(struct zsrv *s)
{
	long n = read(s->fd, s->in, ZSRV_MSIZE);
	if (n <= 0) {
		return -1; /* the client is gone */
	}
	struct msg in = { s->in, (uint32_t)n, 0, false };
	uint32_t size = get(&in, 4);
	uint8_t type = (uint8_t)get(&in, 1);
	uint16_t tag = (uint16_t)get(&in, 2);
	if (in.bad || size != (uint32_t)n) {
		return 0; /* not a message: no answer */
	}
	int rc = perform(s, type, tag, &in);
	if (rc < 0) {
		send_error(s, tag, -rc);
	}
	return 0;
}

static void free_tree(struct zsrv_node *dir)
{
	while (dir->children) {
		struct zsrv_node *n = dir->children;
		dir->children = n->next;
		free_tree(n);
		free(n);
	}
}

void zsrv_free(struct zsrv *s)
{
	while (s->fids) {
		clunk(s, s->fids);
	}
	free_tree(&s->root);
	free(s->in);
	free(s->out);
	free(s);
}
