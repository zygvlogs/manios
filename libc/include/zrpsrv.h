/* Serving files from a program (M12): the ZRP protocol (docs/zrp.md)
 * over a pipe, whose other end the kernel mounts with mountfd(). The
 * library keeps a tree of nodes and the clients' fids, and answers
 * version, attach, walk, stat, clunk and directory reads itself; the
 * program supplies what its files read and write.
 *
 * A read may be deferred -- answered later, when there is something to
 * say (a window waiting for input). The kernel keeps any number of
 * requests in flight on a channel, so a deferred read holds up no one. */
#ifndef MANIOS_ZRPSRV_H
#define MANIOS_ZRPSRV_H

#include <stdbool.h>
#include <stdint.h>
#include <zkt_abi.h>

#define ZSRV_MSIZE 16384
#define ZSRV_DEFER (-0x10000L) /* from a read callback: will answer later */

struct zsrv;

struct zsrv_node {
	char name[ZKT_NAME_MAX + 1];
	uint32_t type;   /* ZKT_TYPE_DIR or ZKT_TYPE_FILE */
	uint32_t length; /* what stat reports */
	void *aux;       /* the program's */
	struct zsrv_node *parent, *children, *next;
	int refs;        /* fids on it: a removed node lives until they go */
	bool removed;
};

struct zsrv_fid {
	uint32_t id;
	struct zsrv_node *node;
	void *aux; /* the program's per-open state */
	struct zsrv_fid *next;
};

/* A read request: answered by the callback's return value, or later
 * with zsrv_respond / zsrv_respond_error if it returned ZSRV_DEFER. */
struct zsrv_req {
	uint16_t tag;
	struct zsrv_fid *fid;
	uint32_t offset, count;
	struct zsrv_req *next;
};

struct zsrv_ops {
	/* Fill buf (up to req->count bytes): the length, -errno, or
	 * ZSRV_DEFER. NULL: files read as empty. buf is the server's reply
	 * buffer, so the callback must not answer other requests. */
	long (*read)(struct zsrv *s, struct zsrv_req *req, void *buf);
	/* The bytes taken, or -errno. NULL: files are read-only (EROFS). */
	long (*write)(struct zsrv *s, struct zsrv_fid *f, uint32_t offset, const void *buf,
	              uint32_t count);
	/* Before a fid goes (the client closed it). Optional. */
	void (*clunk)(struct zsrv *s, struct zsrv_fid *f);
};

struct zsrv {
	int fd;
	const struct zsrv_ops *ops;
	void *aux;
	struct zsrv_node root;
	struct zsrv_fid *fids;
	struct zsrv_req *deferred;
	uint32_t msize;
	uint8_t *in, *out;
};

/* A server on pipe end fd, with an empty root directory; NULL if out of
 * memory. */
struct zsrv *zsrv_new(int fd, const struct zsrv_ops *ops, void *aux);
struct zsrv_node *zsrv_add(struct zsrv_node *dir, const char *name, uint32_t type, void *aux);
/* Takes a node (and its children) out of the tree; fids on them fail
 * from now on, and deferred reads on them are answered with EIO. */
void zsrv_remove(struct zsrv *s, struct zsrv_node *node);

/* Reads one request from the pipe and answers it (or defers it).
 * Returns 0, or -1 once the client side is gone (end of file): the
 * kernel has then clunked every fid it had, unless it lost track of one
 * (zsrv_fid_count tells). Call it when poll() says the pipe is readable. */
int zsrv_handle(struct zsrv *s);
/* Clunks what is left (calling ops->clunk) and frees the server and its
 * tree. Does not close the pipe. */
void zsrv_free(struct zsrv *s);

void zsrv_respond(struct zsrv *s, struct zsrv_req *req, const void *data, uint32_t len);
void zsrv_respond_error(struct zsrv *s, struct zsrv_req *req, int err);

/* Fids currently open (for tests: 0 once every client has let go). */
int zsrv_fid_count(const struct zsrv *s);

#endif
