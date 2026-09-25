#ifndef ZKT_FS_VFS_SELFTEST_H
#define ZKT_FS_VFS_SELFTEST_H

/* Checks path resolution, bind/union semantics and namespace isolation
 * in a thread with a private namespace, then checks that nothing leaked
 * into the caller's namespace, the heap, or vnode reference counts.
 * Needs no disk. Panics naming the first failed check. */
void vfs_selftest(void);

#endif
