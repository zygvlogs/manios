#ifndef ZKT_FS_DEVFS_H
#define ZKT_FS_DEVFS_H

#include "vfs.h"

/* A directory listing every registered device (M5's registry), each as
 * a file. Character devices read and write as byte streams (the offset
 * is ignored); block devices read at byte offsets and report their size.
 * The kernel binds it at /dev. */
struct vnode *devfs_root(void);

#endif
