#ifndef ZKT_FS_FAT_H
#define ZKT_FS_FAT_H

#include <stddef.h>
#include "device.h"
#include "vfs.h"

/* Read-only FAT12/FAT16, 512-byte sectors, 8.3 names (long-name
 * entries are skipped; such files appear under their short alias,
 * e.g. "manios~1.txt"). On success stores the root directory and a short
 * description ("FAT16, 8 MiB"). Returns -EINVAL if `dev` holds no
 * supported FAT volume (FAT32 included, for now). */
int fat_mount(struct device *dev, struct vnode **root, char *desc, size_t desc_len);

#endif
