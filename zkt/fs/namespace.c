#include "namespace.h"
#include <stdbool.h>
#include "cpu.h"
#include "heap.h"
#include "kerrno.h"
#include "kstring.h"
#include "mutex.h"

struct mount {
	char path[VFS_PATH_MAX + 1];
	struct location loc;
	struct mount *next;
};

struct namespace {
	uint32_t refs;
	struct mutex lock; /* the mount table */
	struct vnode *root;
	struct mount *mounts;
};

static void set_label(struct location *loc, size_t i, const char *label)
{
	strlcpy(loc->label[i], label, LOCATION_LABEL_MAX);
}

static void location_copy(struct location *dst, const struct location *src)
{
	*dst = *src;
	for (size_t i = 0; i < dst->count; i++) {
		vnode_ref(dst->v[i]);
	}
}

void location_release(struct location *loc)
{
	for (size_t i = 0; i < loc->count; i++) {
		vnode_unref(loc->v[i]);
	}
	loc->count = 0;
}

struct namespace *ns_create(struct vnode *root)
{
	struct namespace *ns = kmalloc(sizeof(*ns));
	if (!ns) {
		return 0;
	}
	ns->refs = 1;
	ns->lock = (struct mutex)MUTEX_INIT;
	ns->root = root;
	ns->mounts = 0;
	vnode_ref(root);
	return ns;
}

void ns_ref(struct namespace *ns)
{
	if (ns) {
		uint32_t flags = cpu_irq_save();
		ns->refs++;
		cpu_irq_restore(flags);
	}
}

void ns_unref(struct namespace *ns)
{
	if (!ns) {
		return;
	}
	uint32_t flags = cpu_irq_save();
	bool last = --ns->refs == 0;
	cpu_irq_restore(flags);
	if (!last) {
		return;
	}
	/* Unshared now, so no one else can be holding the lock. */
	while (ns->mounts) {
		struct mount *m = ns->mounts;
		ns->mounts = m->next;
		location_release(&m->loc);
		kfree(m);
	}
	vnode_unref(ns->root);
	kfree(ns);
}

struct namespace *ns_fork(struct namespace *src)
{
	struct namespace *ns = ns_create(src->root);
	if (!ns) {
		return 0;
	}
	struct mount **tail = &ns->mounts;
	mutex_lock(&src->lock);
	for (struct mount *m = src->mounts; m; m = m->next) {
		struct mount *copy = kmalloc(sizeof(*copy));
		if (!copy) {
			mutex_unlock(&src->lock);
			ns_unref(ns);
			return 0;
		}
		strlcpy(copy->path, m->path, sizeof(copy->path));
		location_copy(&copy->loc, &m->loc);
		copy->next = 0;
		*tail = copy;
		tail = &copy->next;
	}
	mutex_unlock(&src->lock);
	return ns;
}

int vfs_clean_path(const char *path, char *out)
{
	if (path[0] != '/') {
		return -EINVAL;
	}
	size_t len = 1;
	out[0] = '/';

	const char *p = path;
	while (*p) {
		while (*p == '/') {
			p++;
		}
		const char *name = p;
		while (*p && *p != '/') {
			p++;
		}
		size_t n = (size_t)(p - name);
		if (n == 0 || (n == 1 && name[0] == '.')) {
			continue;
		}
		if (n == 2 && name[0] == '.' && name[1] == '.') {
			while (len > 1 && out[len - 1] != '/') {
				len--;
			}
			if (len > 1) {
				len--; /* the '/' before the removed name */
			}
			continue;
		}
		if (n > VFS_NAME_MAX || len + 1 + n > VFS_PATH_MAX) {
			return -ENAMETOOLONG;
		}
		if (len > 1) {
			out[len++] = '/';
		}
		memcpy(out + len, name, n);
		len += n;
	}
	out[len] = '\0';
	return 0;
}

static struct mount *find_mount(struct namespace *ns, const char *canon)
{
	for (struct mount *m = ns->mounts; m; m = m->next) {
		if (strcmp(m->path, canon) == 0) {
			return m;
		}
	}
	return 0;
}

static void apply_mount(struct namespace *ns, const char *canon, struct location *loc)
{
	struct mount *m = find_mount(ns, canon);
	if (m) {
		location_release(loc);
		location_copy(loc, &m->loc);
	}
}

/* Union semantics: the first member that has the name wins. */
static int walk_location(const struct location *loc, const char *name, struct vnode **out)
{
	int rc = -ENOTDIR;
	for (size_t i = 0; i < loc->count; i++) {
		struct vnode *v = loc->v[i];
		if (v->type != VNODE_DIR || !v->ops->walk) {
			continue;
		}
		int r = v->ops->walk(v, name, out);
		if (r == 0) {
			return 0;
		}
		rc = r;
	}
	return rc;
}

int ns_resolve(struct namespace *ns, const char *path, struct location *loc, char *canon)
{
	char clean[VFS_PATH_MAX + 1];
	char prefix[VFS_PATH_MAX + 1];
	int rc = vfs_clean_path(path, clean);
	if (rc) {
		return rc;
	}

	mutex_lock(&ns->lock);
	loc->count = 1;
	loc->v[0] = ns->root;
	vnode_ref(ns->root);
	set_label(loc, 0, "/");
	apply_mount(ns, "/", loc);

	size_t prefix_len = 0;
	const char *p = clean + 1;
	while (*p) {
		char name[VFS_NAME_MAX + 1];
		size_t n = 0;
		while (p[n] && p[n] != '/') {
			n++;
		}
		memcpy(name, p, n);
		name[n] = '\0';

		struct vnode *next;
		rc = walk_location(loc, name, &next);
		location_release(loc);
		if (rc) {
			mutex_unlock(&ns->lock);
			return rc;
		}

		prefix[prefix_len++] = '/';
		memcpy(prefix + prefix_len, name, n);
		prefix_len += n;
		prefix[prefix_len] = '\0';

		loc->count = 1;
		loc->v[0] = next;
		set_label(loc, 0, prefix);
		apply_mount(ns, prefix, loc);

		p += n;
		if (*p == '/') {
			p++;
		}
	}
	mutex_unlock(&ns->lock);

	if (canon) {
		strlcpy(canon, clean, VFS_PATH_MAX + 1);
	}
	return 0;
}

int ns_set_mount(struct namespace *ns, const char *canon, struct location *loc)
{
	mutex_lock(&ns->lock);
	struct mount *m = find_mount(ns, canon);
	if (m) {
		location_release(&m->loc);
	} else {
		m = kmalloc(sizeof(*m));
		if (!m) {
			mutex_unlock(&ns->lock);
			location_release(loc);
			return -ENOMEM;
		}
		strlcpy(m->path, canon, sizeof(m->path));
		m->next = 0;
		struct mount **tail = &ns->mounts;
		while (*tail) {
			tail = &(*tail)->next;
		}
		*tail = m;
	}
	m->loc = *loc;
	loc->count = 0;
	mutex_unlock(&ns->lock);
	return 0;
}

int ns_remove_mount(struct namespace *ns, const char *canon)
{
	mutex_lock(&ns->lock);
	struct mount **link = &ns->mounts;
	while (*link && strcmp((*link)->path, canon) != 0) {
		link = &(*link)->next;
	}
	struct mount *m = *link;
	if (m) {
		*link = m->next;
	}
	mutex_unlock(&ns->lock);

	if (!m) {
		return -ENOENT;
	}
	location_release(&m->loc);
	kfree(m);
	return 0;
}

void ns_foreach(struct namespace *ns,
                void (*fn)(const char *path, const struct location *loc, void *ctx),
                void *ctx)
{
	mutex_lock(&ns->lock);
	for (struct mount *m = ns->mounts; m; m = m->next) {
		fn(m->path, &m->loc, ctx);
	}
	mutex_unlock(&ns->lock);
}
