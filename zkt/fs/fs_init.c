#include "fs_init.h"
#include "devfs.h"
#include "device.h"
#include "fat.h"
#include "kprintf.h"
#include "namespace.h"
#include "panic.h"
#include "ramfs.h"
#include "sched.h"

void fs_init(void)
{
	struct vnode *root = ramfs_create();
	if (!root || ramfs_mkdir(root, "dev") || ramfs_mkdir(root, "n")) {
		panic("fs_init: cannot build the root directory");
	}
	struct namespace *ns = ns_create(root);
	if (!ns) {
		panic("fs_init: cannot create the kernel namespace");
	}
	thread_set_namespace(ns);
	ns_unref(ns); /* the thread's reference keeps it */

	if (vfs_mount(devfs_root(), "devfs", "/dev", BIND_REPLACE) != 0) {
		panic("fs_init: cannot bind devfs at /dev");
	}

	/* The FAT roots keep the reference fat_mount() returned, so they stay
	 * alive even if the mount is later unbound. */
	for (struct device *d = device_next(0); d; d = device_next(d)) {
		struct vnode *fat_root;
		char desc[32], dir[8 + DEVICE_NAME_MAX], path[8 + DEVICE_NAME_MAX], label[32];
		if (d->class != DEVICE_BLOCK || fat_mount(d, &fat_root, desc, sizeof(desc)) != 0) {
			continue;
		}
		ksnprintf(dir, sizeof(dir), "n/%s", d->name);
		ksnprintf(path, sizeof(path), "/n/%s", d->name);
		ksnprintf(label, sizeof(label), "fat:%s", d->name);
		if (ramfs_mkdir(root, dir) == 0 && vfs_mount(fat_root, label, path, BIND_REPLACE) == 0) {
			kprintf("%s: %s, mounted at %s\n", d->name, desc, path);
		}
	}
}
