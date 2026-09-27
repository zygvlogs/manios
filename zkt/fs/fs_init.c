#include "fs_init.h"
#include "bootfs.h"
#include "devfs.h"
#include "device.h"
#include "fat.h"
#include "iso9660.h"
#include "kstring.h"
#include "kprintf.h"
#include "namespace.h"
#include "panic.h"
#include "ramfs.h"
#include "sched.h"

void fs_init(void)
{
	struct vnode *root = ramfs_create();
	if (!root || ramfs_mkdir(root, "dev") || ramfs_mkdir(root, "n")
	    || ramfs_mkdir(root, "boot") || ramfs_mkdir(root, "bin") || ramfs_mkdir(root, "mnt/term")) {
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

	/* The boot archive at /boot, and its programs at /bin -- as a bind,
	 * so a disk's bin/ can later be unioned in front or behind. */
	struct vnode *boot = bootfs_create();
	if (!boot || vfs_mount(boot, "bootfs", "/boot", BIND_REPLACE) != 0
	    || vfs_bind("/boot/bin", "/bin", BIND_REPLACE) != 0) {
		panic("fs_init: cannot mount the boot archive");
	}

	/* Each disk's file system at /n/NAME: FAT, or ISO 9660. A drive with
	 * removable media (a CD drive) gets a root that follows its disc.
	 * The roots keep the reference the mount returned, so they stay
	 * alive even if the mount is later unbound. */
	for (struct device *d = device_next(0); d; d = device_next(d)) {
		struct vnode *fs_root;
		char desc[64], dir[8 + DEVICE_NAME_MAX], path[8 + DEVICE_NAME_MAX], label[32];
		const char *kind = "fat";
		if (d->class != DEVICE_BLOCK) {
			continue;
		}
		if (d->block_ops->check_media) {
			kind = "iso9660";
			if (iso9660_mount_removable(d, &fs_root) != 0) {
				continue;
			}
			strlcpy(desc, "the disc in the drive", sizeof(desc));
		} else if (fat_mount(d, &fs_root, desc, sizeof(desc)) != 0) {
			kind = "iso9660";
			if (iso9660_mount(d, &fs_root, desc, sizeof(desc)) != 0) {
				continue;
			}
		}
		ksnprintf(dir, sizeof(dir), "n/%s", d->name);
		ksnprintf(path, sizeof(path), "/n/%s", d->name);
		ksnprintf(label, sizeof(label), "%s:%s", kind, d->name);
		if (ramfs_mkdir(root, dir) == 0 && vfs_mount(fs_root, label, path, BIND_REPLACE) == 0) {
			kprintf("%s: %s, mounted at %s\n", d->name, desc, path);
		}
	}
}
