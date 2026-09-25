/* ZRP, the ZygKernel Resource Protocol, version "ZRP1" (ADR-0003): a
 * 9P-style protocol for reaching a file tree on another node. The wire
 * format is specified in docs/zrp.md; this header mirrors it.
 *
 * Every message: size[4] type[1] tag[2] body, little-endian; strings
 * are len[2] bytes. Over UDP each datagram carries one message. */
#ifndef ZKT_NET_ZRP_H
#define ZKT_NET_ZRP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "vfs.h"

#define ZRP_PORT 5640
#define ZRP_VERSION "ZRP1"
#define ZRP_MSIZE 1472 /* the largest UDP payload one Ethernet frame carries */
#define ZRP_CHANNEL_MSIZE 16384 /* over a local channel (a pipe) */
#define ZRP_MSIZE_MIN 256
#define ZRP_HEADER 7
#define ZRP_NOTAG 0xFFFF
#define ZRP_READ_OVERHEAD (ZRP_HEADER + 4) /* Rread: header, count */
#define ZRP_WRITE_OVERHEAD (ZRP_HEADER + 12) /* Twrite: header, fid, offset, count */

enum zrp_type {
	ZRP_TVERSION = 1, ZRP_RVERSION, /* msize[4] version[s] -> msize[4] version[s] */
	ZRP_TATTACH, ZRP_RATTACH,       /* fid[4] aname[s] -> stat */
	ZRP_TWALK, ZRP_RWALK,           /* fid[4] newfid[4] name[s] -> stat */
	ZRP_TREAD, ZRP_RREAD,           /* fid[4] offset[4] count[4] -> count[4] data */
	ZRP_TWRITE, ZRP_RWRITE,         /* fid[4] offset[4] count[4] data -> count[4] */
	ZRP_TCLUNK, ZRP_RCLUNK,         /* fid[4] -> */
	ZRP_TSTAT, ZRP_RSTAT,           /* fid[4] -> stat */
	ZRP_RERROR = 64,                /* -> errno[2] (zkt_abi.h numbers) */
};

/* stat: type[1] (ZKT_TYPE_*) length[4] name[s] */

/* A cursor over a message buffer. Any overrun sets `bad` and further
 * gets return zeros, so a parser checks once at the end. */
struct zmsg {
	uint8_t *buf;
	size_t cap, pos;
	bool bad;
};

void zmsg_start(struct zmsg *m, uint8_t *buf, size_t cap, uint8_t type, uint16_t tag);
/* Writes the size field; returns the message length (0 if it overflowed). */
size_t zmsg_finish(struct zmsg *m);
void zmsg_put8(struct zmsg *m, uint8_t v);
void zmsg_put16(struct zmsg *m, uint16_t v);
void zmsg_put32(struct zmsg *m, uint32_t v);
void zmsg_putstr(struct zmsg *m, const char *s);
void zmsg_putbytes(struct zmsg *m, const void *data, size_t len);
void zmsg_putstat(struct zmsg *m, enum vnode_type type, uint32_t length, const char *name);

/* For parsing a received message of `len` bytes (header included). */
void zmsg_open(struct zmsg *m, uint8_t *buf, size_t len);
uint8_t zmsg_get8(struct zmsg *m);
uint16_t zmsg_get16(struct zmsg *m);
uint32_t zmsg_get32(struct zmsg *m);
/* Copies a string, NUL-terminated; too long for `size` is bad. */
void zmsg_getstr(struct zmsg *m, char *out, size_t size);
const uint8_t *zmsg_getbytes(struct zmsg *m, size_t len);
bool zmsg_getstat(struct zmsg *m, enum vnode_type *type, uint32_t *length, char *name, size_t size);

/* --- the server --- */

struct zrp_server;
/* Serves `root` (a directory path in the calling thread's namespace) on
 * UDP `port`, from a new kernel thread. NULL with *err on failure. */
struct zrp_server *zrp_serve(const char *root, uint16_t port, int *err);
/* Stops the server and waits for its thread to finish. */
void zrp_server_stop(struct zrp_server *srv);

struct zrp_server_stats {
	uint32_t requests, duplicates, errors, sessions, fids;
};
void zrp_server_stats(struct zrp_server *srv, struct zrp_server_stats *out);

/* --- the client --- */

/* Connects to a ZRP server and attaches: `dial` is "udp!A.B.C.D!PORT",
 * "udp!A.B.C.D" or "A.B.C.D" (port 5640). Stores a reference to the
 * root vnode, and a label for the mount table. */
int zrp_mount(const char *dial, const char *aname, struct vnode **root, char *label, size_t size);

/* The same over a channel: `chan` is a pipe end whose other end a ZRP
 * server reads and writes (SYS_MOUNTFD). The session takes its own
 * reference to it. */
int zrp_mount_channel(struct vnode *chan, const char *aname, struct vnode **root, char *label,
                      size_t size);

/* Client requests retransmitted since boot (for tests and `net`). */
uint32_t zrp_client_retransmits(void);

#endif
