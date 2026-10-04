#include "vfs.h"
#include <stdbool.h>
#include "cpu.h"
#include "heap.h"
#include "kerrno.h"
#include "zkt_abi.h"
#include "kstring.h"
#include "namespace.h"
#include "sched.h"

void vnode_ref(struct vnode *v)
{
	uint32_t flags = cpu_irq_save();
	v->refs++;
	cpu_irq_restore(flags);
}

void vnode_unref(struct vnode *v)
{
	uint32_t flags = cpu_irq_save();
	bool last = --v->refs == 0;
	cpu_irq_restore(flags);
	if (last && v->ops->release) {
		v->ops->release(v);
	}
}

struct file {
	uint32_t refs;
	int mode;
	char name[VFS_NAME_MAX + 1];
	struct location loc;
	uint32_t offset; /* files and devices */
	size_t member;   /* directories: union member being listed... */
	uint32_t index;  /* ...and the next entry within it... */
	uint32_t entry;  /* ...which is entry number `entry` of the whole listing */
};

static bool can_read(const struct file *f)
{
	return f->mode == OREAD || f->mode == ORDWR;
}

static bool can_write(const struct file *f)
{
	return f->mode == OWRITE || f->mode == ORDWR;
}

/* The directory a path's last element goes in, and that element. The
 * path is cleaned first, so `leaf` is a plain name, never "." or "..",
 * and never empty (the root itself holds nothing to change). `ppath`,
 * when the caller wants it, is that directory's path. */
static int parent_of(const char *path, struct location *loc, char *leaf, char *ppath)
{
	char clean[VFS_PATH_MAX + 1];
	int rc = vfs_clean_path(path, clean);
	if (rc) {
		return rc;
	}
	char *slash = 0;
	for (char *p = clean; *p; p++) {
		if (*p == '/') {
			slash = p;
		}
	}
	if (!slash || !slash[1]) {
		return -EINVAL; /* the root itself holds nothing to change */
	}
	*slash = '\0';
	if (ppath) {
		strlcpy(ppath, slash == clean ? "/" : clean, VFS_PATH_MAX + 1);
	}
	rc = ns_resolve(thread_namespace(), clean[0] ? clean : "/", loc, 0);
	if (rc) {
		return rc;
	}
	strlcpy(leaf, slash + 1, VFS_NAME_MAX + 1);
	return 0;
}

/* The first member of a union directory is where changes go. */
static struct vnode *writable_dir(struct location *loc)
{
	struct vnode *dir = loc->v[0];
	return dir->type == VNODE_DIR ? dir : 0;
}

int vfs_create(const char *path, enum vnode_type type, struct vnode **out)
{
	struct location loc;
	char leaf[VFS_NAME_MAX + 1];
	*out = 0;
	if (type != VNODE_FILE && type != VNODE_DIR) {
		return -EINVAL;
	}
	int rc = parent_of(path, &loc, leaf, 0);
	if (rc) {
		return rc;
	}
	struct vnode *dir = writable_dir(&loc);
	rc = !dir ? -ENOTDIR : !dir->ops->create ? -EROFS : dir->ops->create(dir, leaf, type, out);
	location_release(&loc);
	return rc;
}

/* Whether the name is in `dir` at all, for a file system that can't
 * say for itself: POSIX gives ENOENT for a name that isn't there
 * before its own answer (EROFS, for one it takes no writes on). */
static int exists_for(struct vnode *dir, const char *leaf)
{
	struct vnode *found = 0;
	int look = dir->ops->walk ? dir->ops->walk(dir, leaf, &found) : 0;
	if (found) {
		vnode_unref(found);
	}
	return look ? look : -EROFS;
}

/* Removes one name from its directory: a file (vfs_unlink) or an empty
 * directory (vfs_rmdir). */
static int vfs_remove(const char *path, enum vnode_type expect)
{
	struct location loc;
	char leaf[VFS_NAME_MAX + 1];
	int rc = parent_of(path, &loc, leaf, 0);
	if (rc) {
		return rc;
	}
	struct vnode *dir = writable_dir(&loc);
	if (!dir) {
		rc = -ENOTDIR;
	} else if (dir->ops->remove) {
		rc = dir->ops->remove(dir, leaf, expect);
	} else {
		rc = exists_for(dir, leaf);
	}
	location_release(&loc);
	return rc;
}

int vfs_unlink(const char *path)
{
	return vfs_remove(path, VNODE_FILE);
}

int vfs_rmdir(const char *path)
{
	return vfs_remove(path, VNODE_DIR);
}

int vfs_rename(const char *from, const char *to)
{
	struct location a, b;
	char leaf_a[VFS_NAME_MAX + 1], leaf_b[VFS_NAME_MAX + 1];
	char pa[VFS_PATH_MAX + 1], pb[VFS_PATH_MAX + 1];
	int rc = parent_of(from, &a, leaf_a, pa);
	if (rc) {
		return rc;
	}
	rc = parent_of(to, &b, leaf_b, pb);
	if (rc) {
		location_release(&a);
		return rc;
	}
	struct vnode *dir = writable_dir(&a);
	/* One directory by two routes: a walk hands out a vnode per call,
	 * so the same directory resolved twice is two vnodes, and its path
	 * says it (EXDEV only when the two paths really are two). */
	bool same = dir == writable_dir(&b) || strcmp(pa, pb) == 0;
	if (!same) {
		rc = -EXDEV; /* one directory: FAT renames nothing across two */
	} else if (!dir) {
		rc = -ENOTDIR;
	} else if (dir->ops->rename) {
		rc = dir->ops->rename(dir, leaf_a, leaf_b);
	} else {
		rc = exists_for(dir, leaf_a);
	}
	location_release(&a);
	location_release(&b);
	return rc;
}

int vfs_open(const char *path, int mode, struct file **out)
{
	int access = mode & 3;
	int flags = mode & ~3;
	if ((access != OREAD && access != OWRITE && access != ORDWR)
	    || (flags & ~(O_CREAT | O_TRUNC | O_EXCL))) {
		return -EINVAL;
	}
	struct file *f = kmalloc(sizeof(*f));
	if (!f) {
		return -ENOMEM;
	}
	char canon[VFS_PATH_MAX + 1];
	int rc = ns_resolve(thread_namespace(), path, &f->loc, canon);
	bool existed = rc == 0;
	if (rc == -ENOENT && (flags & O_CREAT)) {
		struct vnode *v = 0;
		rc = vfs_create(path, VNODE_FILE, &v);
		if (!rc) {
			f->loc.count = 1;
			f->loc.v[0] = v;
			f->loc.label[0][0] = '\0';
			rc = vfs_clean_path(path, canon);
		}
	}
	if (rc) {
		kfree(f);
		return rc;
	}
	struct vnode *v = f->loc.v[0];
	if (((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) && existed) {
		rc = -EEXIST;
	} else if (access != OREAD && v->type == VNODE_DIR) {
		rc = -EISDIR; /* a directory is listed, never written */
	} else if ((flags & O_TRUNC) && access != OREAD && v->type == VNODE_FILE) {
		/* Only a file shortens: POSIX leaves O_TRUNC on anything else
		 * to the file system, and a device or pipe has no size. */
		rc = !v->ops->truncate ? -EROFS : v->ops->truncate(v, 0);
	} else if (access != OREAD && !v->ops->write) {
		rc = -EROFS;
	}
	if (rc) {
		location_release(&f->loc);
		kfree(f);
		return rc;
	}
	f->refs = 1;
	f->mode = access;
	const char *slash = canon;
	for (const char *c = canon; *c; c++) {
		if (*c == '/') {
			slash = c;
		}
	}
	strlcpy(f->name, canon[1] ? slash + 1 : "/", sizeof(f->name));
	f->offset = 0;
	f->member = 0;
	f->index = 0;
	f->entry = 0;
	*out = f;
	return 0;
}

int vfs_open_vnode(struct vnode *v, int mode, const char *name, struct file **out)
{
	struct file *f = kmalloc(sizeof(*f));
	if (!f) {
		return -ENOMEM;
	}
	memset(f, 0, sizeof(*f));
	f->refs = 1;
	f->mode = mode;
	f->loc.count = 1;
	f->loc.v[0] = v;
	strlcpy(f->name, name, sizeof(f->name));
	*out = f;
	return 0;
}

long vfs_read(struct file *f, void *buf, size_t len)
{
	struct vnode *v = f->loc.v[0];
	if (!can_read(f)) {
		return -EBADF;
	}
	if (v->type == VNODE_DIR) {
		return -EISDIR;
	}
	if (!v->ops->read) {
		return -ENODEV;
	}
	long n = v->ops->read(v, f->offset, buf, len);
	if (n > 0) {
		f->offset += (uint32_t)n;
	}
	return n;
}

long vfs_pread(struct file *f, void *buf, size_t len, uint32_t offset)
{
	struct vnode *v = f->loc.v[0];
	if (!can_read(f)) {
		return -EBADF;
	}
	if (v->type == VNODE_DIR) {
		return -EISDIR;
	}
	return v->ops->read ? v->ops->read(v, offset, buf, len) : -ENODEV;
}

long vfs_pwrite(struct file *f, const void *buf, size_t len, uint32_t offset)
{
	struct vnode *v = f->loc.v[0];
	if (!can_write(f)) {
		return -EBADF;
	}
	return v->ops->write(v, offset, buf, len);
}

long vfs_write(struct file *f, const void *buf, size_t len)
{
	struct vnode *v = f->loc.v[0];
	if (!can_write(f)) {
		return -EBADF;
	}
	long n = v->ops->write(v, f->offset, buf, len);
	if (n > 0) {
		f->offset += (uint32_t)n;
	}
	return n;
}

struct file *vfs_dup(struct file *f)
{
	uint32_t flags = cpu_irq_save();
	f->refs++;
	cpu_irq_restore(flags);
	return f;
}

const char *vfs_name(const struct file *f)
{
	return f->name;
}

int vfs_readdir(struct file *f, struct dirent *out)
{
	if (f->loc.v[0]->type != VNODE_DIR) {
		return -ENOTDIR;
	}
	while (f->member < f->loc.count) {
		struct vnode *v = f->loc.v[f->member];
		int rc = (v->type == VNODE_DIR && v->ops->readdir)
		         ? v->ops->readdir(v, f->index, out) : 0;
		if (rc == 1) {
			f->index++;
			f->entry++;
			return 1;
		}
		if (rc < 0) {
			return rc;
		}
		f->member++;
		f->index = 0;
	}
	return 0;
}

bool vfs_is_pipe(const struct file *f)
{
	return f->loc.v[0]->type == VNODE_PIPE;
}

long vfs_seek(struct file *f, int32_t offset, int whence)
{
	if (f->loc.v[0]->type == VNODE_DIR) {
		if (offset != 0 || whence != SEEK_SET) {
			return -EINVAL;
		}
		f->member = 0;
		f->index = 0;
		f->entry = 0;
		return 0;
	}
	int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (int64_t)f->offset
	             : whence == SEEK_END ? (int64_t)f->loc.v[0]->size : -1;
	int64_t target = base + offset;
	if (base < 0 || target < 0 || target > 0x7FFFFFFF) {
		return -EINVAL;
	}
	f->offset = (uint32_t)target;
	return (long)target;
}

int vfs_readdir_at(struct file *f, uint32_t entry, struct dirent *out)
{
	if (entry < f->entry) {
		f->member = 0;
		f->index = 0;
		f->entry = 0;
	}
	while (f->entry < entry) {
		struct dirent skipped;
		int rc = vfs_readdir(f, &skipped);
		if (rc <= 0) {
			return rc;
		}
	}
	return vfs_readdir(f, out);
}

struct vnode *vfs_vnode(struct file *f)
{
	return f->loc.v[0];
}

int vfs_poll(struct file *f)
{
	struct vnode *v = f->loc.v[0];
	return v->ops->poll ? v->ops->poll(v) : 1;
}

enum vnode_type vfs_type(const struct file *f)
{
	return f->loc.v[0]->type;
}

uint32_t vfs_size(const struct file *f)
{
	return f->loc.v[0]->size;
}

void vfs_close(struct file *f)
{
	uint32_t flags = cpu_irq_save();
	bool last = --f->refs == 0;
	cpu_irq_restore(flags);
	if (last) {
		location_release(&f->loc);
		kfree(f);
	}
}

/* Composes and installs a bind. Consumes both locations' references. */
static int bind_locations(struct namespace *ns, struct location *from, struct location *onto,
                          const char *old_canon, enum bind_flag flag)
{
	bool from_dir = from->v[0]->type == VNODE_DIR;
	bool onto_dir = onto->v[0]->type == VNODE_DIR;
	int rc = 0;
	struct location result = { 0 };

	if (from_dir != onto_dir) {
		rc = onto_dir ? -ENOTDIR : -EISDIR;
	} else if (flag != BIND_REPLACE && !onto_dir) {
		rc = -ENOTDIR; /* only directories form unions */
	} else {
		const struct location *first = flag == BIND_AFTER ? onto : from;
		const struct location *second = flag == BIND_REPLACE ? 0
		                                : flag == BIND_AFTER ? from : onto;
		size_t total = first->count + (second ? second->count : 0);
		if (total > VFS_UNION_MAX) {
			rc = -EINVAL;
		} else {
			for (size_t i = 0; i < first->count; i++, result.count++) {
				result.v[result.count] = first->v[i];
				memcpy(result.label[result.count], first->label[i], LOCATION_LABEL_MAX);
				vnode_ref(first->v[i]);
			}
			for (size_t i = 0; second && i < second->count; i++, result.count++) {
				result.v[result.count] = second->v[i];
				memcpy(result.label[result.count], second->label[i], LOCATION_LABEL_MAX);
				vnode_ref(second->v[i]);
			}
			rc = ns_set_mount(ns, old_canon, &result);
		}
	}
	location_release(from);
	location_release(onto);
	return rc;
}

int vfs_bind(const char *new_path, const char *old_path, enum bind_flag flag)
{
	struct namespace *ns = thread_namespace();
	struct location from, onto;
	char old_canon[VFS_PATH_MAX + 1];

	int rc = ns_resolve(ns, new_path, &from, 0);
	if (rc) {
		return rc;
	}
	rc = ns_resolve(ns, old_path, &onto, old_canon);
	if (rc) {
		location_release(&from);
		return rc;
	}
	return bind_locations(ns, &from, &onto, old_canon, flag);
}

int vfs_mount(struct vnode *root, const char *label, const char *old_path, enum bind_flag flag)
{
	struct namespace *ns = thread_namespace();
	struct location from = { .count = 1, .v = { root } };
	struct location onto;
	char old_canon[VFS_PATH_MAX + 1];

	strlcpy(from.label[0], label, LOCATION_LABEL_MAX);
	int rc = ns_resolve(ns, old_path, &onto, old_canon);
	if (rc) {
		return rc;
	}
	vnode_ref(root);
	return bind_locations(ns, &from, &onto, old_canon, flag);
}

int vfs_unbind(const char *old_path)
{
	char canon[VFS_PATH_MAX + 1];
	int rc = vfs_clean_path(old_path, canon);
	return rc ? rc : ns_remove_mount(thread_namespace(), canon);
}
