#ifndef ZKT_FS_RAMFS_H
#define ZKT_FS_RAMFS_H

#include "vfs.h"

/* An in-memory tree of directories (no files yet). The kernel's root
 * is one; tests build private ones. */

/* Returns the root of a new, empty tree, or NULL when out of memory.
 * The tree holds a reference on every node, including the root. */
struct vnode *ramfs_create(void);

/* Creates `path` (relative to the tree's root, e.g. "n/ata0p1") and any
 * missing parents. Existing directories are fine. */
int ramfs_mkdir(struct vnode *root, const char *path);

/* Frees the whole tree. Panics if anything outside the tree still holds
 * a reference to one of its nodes, so leaked references show up. */
void ramfs_destroy(struct vnode *root);

#endif
