#ifndef ZKT_FS_BOOTFS_H
#define ZKT_FS_BOOTFS_H

#include "vfs.h"

/* The boot archive: a ustar archive of user programs linked into the
 * kernel image by the build (build/bootfs.tar), so ManiOS has userspace
 * even without a disk. Returns it as a read-only ramfs tree, or NULL if
 * the archive is malformed. */
struct vnode *bootfs_create(void);

#endif
