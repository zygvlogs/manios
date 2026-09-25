/* Per-thread namespaces (ADR-0003): a root plus a mount table mapping
 * cleaned absolute paths to ordered unions of vnodes. Threads inherit
 * their creator's namespace by reference; ns_fork() gives a private
 * copy, as Plan 9's rfork(RFNAMEG) does. Processes take this over at M8. */
#ifndef ZKT_FS_NAMESPACE_H
#define ZKT_FS_NAMESPACE_H

#include "vfs.h"

#define LOCATION_LABEL_MAX 32

/* What a path resolved to: one vnode, or the members of a union in
 * search order. Holds a reference on each. */
struct location {
	size_t count;
	struct vnode *v[VFS_UNION_MAX];
	char label[VFS_UNION_MAX][LOCATION_LABEL_MAX]; /* where each came from */
};

struct namespace;

struct namespace *ns_create(struct vnode *root);
struct namespace *ns_fork(struct namespace *ns);

/* Both accept NULL. ns_unref never blocks, so it is safe where a thread
 * is being reclaimed, possibly inside an interrupt. */
void ns_ref(struct namespace *ns);
void ns_unref(struct namespace *ns);

/* Lexically cleans an absolute path: collapses "//", drops ".", and
 * applies ".." to the preceding name (".." at the root stays there),
 * like Plan 9's cleanname. `out` holds VFS_PATH_MAX + 1 bytes. */
int vfs_clean_path(const char *path, char *out);

/* Resolves `path` in `ns`. On success `loc` holds references (drop them
 * with location_release) and `canon`, if not NULL, the cleaned path. */
int ns_resolve(struct namespace *ns, const char *path, struct location *loc, char *canon);
void location_release(struct location *loc);

/* Makes `loc` (taking over its references) what `canon` resolves to,
 * replacing any previous entry. */
int ns_set_mount(struct namespace *ns, const char *canon, struct location *loc);
int ns_remove_mount(struct namespace *ns, const char *canon);

/* Calls fn for each mount entry, in the order they were created. */
void ns_foreach(struct namespace *ns,
                void (*fn)(const char *path, const struct location *loc, void *ctx),
                void *ctx);

#endif
