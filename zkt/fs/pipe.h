#ifndef ZKT_FS_PIPE_H
#define ZKT_FS_PIPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "vfs.h"

/* A new pipe: two connected ends, each a vnode holding one reference
 * (pipe.c describes the semantics). */
int pipe_create(struct vnode *ends[2]);
bool vnode_is_pipe(const struct vnode *v);

/* For kernel users (the ZRP channel transport): one message out; bytes
 * of one message in, waiting until timer tick `deadline` (0: forever).
 * 0 means end of file; -ETIMEDOUT, -EPIPE. */
long pipe_send(struct vnode *end, const void *buf, size_t len);
long pipe_recv(struct vnode *end, void *buf, size_t len, uint64_t deadline);

#endif
