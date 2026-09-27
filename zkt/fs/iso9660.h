#ifndef ZKT_FS_ISO9660_H
#define ZKT_FS_ISO9660_H

#include <stddef.h>
#include "device.h"
#include "vfs.h"

/* Read-only ISO 9660 (CDs, DVDs, .iso images), with the long names of
 * Rock Ridge or Joliet when the disc has them. On success stores the
 * root directory and a short description ("ISO 9660, Rock Ridge,
 * 5 MiB, MY_DISC"). Returns -EINVAL if `dev` holds no ISO 9660 volume.
 * Works on 2048-byte blocks (CDs) and 512-byte ones (an ISO image
 * written to a disk or stick). */
int iso9660_mount(struct device *dev, struct vnode **root, char *desc, size_t desc_len);

/* For a drive with removable media: a root that follows the disc in
 * the drive -- empty with none, and mounted afresh when one is changed
 * (files open on the old disc then fail with EIO). Always succeeds but
 * for memory. */
int iso9660_mount_removable(struct device *dev, struct vnode **root);

#endif
