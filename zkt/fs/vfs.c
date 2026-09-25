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
	uint32_t index;  /* ...and the next entry within it */
};

static bool can_read(const struct file *f)
{
	return f->mode == OREAD || f->mode == ORDWR;
}

static bool can_write(const struct file *f)
{
	return f->mode == OWRITE || f->mode == ORDWR;
}

int vfs_open(const char *path, int mode, struct file **out)
{
	if (mode != OREAD && mode != OWRITE && mode != ORDWR) {
		return -EINVAL;
	}
	struct file *f = kmalloc(sizeof(*f));
	if (!f) {
		return -ENOMEM;
	}
	char canon[VFS_PATH_MAX + 1];
	int rc = ns_resolve(thread_namespace(), path, &f->loc, canon);
	if (rc) {
		kfree(f);
		return rc;
	}
	f->refs = 1;
	f->mode = mode;
	struct vnode *v = f->loc.v[0];
	if (can_write(f) && (v->type == VNODE_DIR || !v->ops->write)) {
		location_release(&f->loc);
		kfree(f);
		return v->type == VNODE_DIR ? -EISDIR : -EROFS;
	}
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
