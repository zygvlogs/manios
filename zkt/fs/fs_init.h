#ifndef ZKT_FS_FS_INIT_H
#define ZKT_FS_FS_INIT_H

/* Builds the kernel namespace and gives it to the calling thread: a
 * ramfs root with /dev (devfs) and /n, and every FAT volume found on a
 * block device mounted at /n/<device>. Threads created afterwards
 * inherit it. Needs the drivers. */
void fs_init(void);

#endif
