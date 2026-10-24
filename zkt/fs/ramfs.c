#include "ramfs.h"
#include "heap.h"
#include "kerrno.h"
#include "kstring.h"
#include "panic.h"

struct ram_node {
	struct vnode vnode;
	char name[VFS_NAME_MAX + 1];
	const uint8_t *data;       /* files only */
	uint8_t *owned_data;       /* mutable files only */
	struct ram_node *children; /* in creation order */
	struct ram_node *next;     /* sibling */
};

static struct ram_node *node_of(struct vnode *v)
{
	return (struct ram_node *)v;
}

static int ram_walk(struct vnode *dir, const char *name, struct vnode **out)
{
	for (struct ram_node *c = node_of(dir)->children; c; c = c->next) {
		if (strcmp(c->name, name) == 0) {
			vnode_ref(&c->vnode);
			*out = &c->vnode;
			return 0;
		}
	}
	return -ENOENT;
}

static int ram_readdir(struct vnode *dir, uint32_t index, struct dirent *out)
{
	struct ram_node *c = node_of(dir)->children;
	while (c && index--) {
		c = c->next;
	}
	if (!c) {
		return 0;
	}
	strlcpy(out->name, c->name, sizeof(out->name));
	out->type = c->vnode.type;
	out->size = c->vnode.size;
	return 1;
}

static long ram_read(struct vnode *v, uint32_t offset, void *buf, size_t len)
{
	if (offset >= v->size) {
		return 0;
	}
	if (len > v->size - offset) {
		len = v->size - offset;
	}
	memcpy(buf, node_of(v)->data + offset, len);
	return (long)len;
}

static long ram_write(struct vnode *v, uint32_t offset, const void *buf, size_t len)
{
	struct ram_node *n = node_of(v);
	if (len > UINT32_MAX - offset) return -EINVAL;
	uint32_t end = offset + (uint32_t)len;
	if (!len) return 0;
	uint32_t size = end > v->size ? end : v->size;
	uint8_t *data = kmalloc(size);
	if (!data) return -ENOMEM;
	memset(data, 0, size);
	if (v->size) memcpy(data, n->data, v->size);
	memcpy(data + offset, buf, len);
	if (n->owned_data) kfree(n->owned_data);
	n->owned_data = data;
	n->data = data;
	v->size = size;
	return (long)len;
}

static int ram_truncate(struct vnode *v, uint32_t size)
{
	struct ram_node *n = node_of(v);
	if (size == v->size) return 0;
	uint8_t *data = size ? kmalloc(size) : 0;
	if (size && !data) return -ENOMEM;
	if (size) {
		memset(data, 0, size);
		uint32_t copy = v->size < size ? v->size : size;
		if (copy) memcpy(data, n->data, copy);
	}
	if (n->owned_data) kfree(n->owned_data);
	n->owned_data = data;
	n->data = data;
	v->size = size;
	return 0;
}

/* Nodes belong to their tree: the last reference only ever drops when
 * ramfs_destroy() releases the tree's own, which frees them directly. */
static const struct vnode_ops ram_ops = {
	.walk = ram_walk,
	.read = ram_read,
	.readdir = ram_readdir,
};

static const struct vnode_ops ram_writable_file_ops = {
	.read = ram_read,
	.write = ram_write,
	.truncate = ram_truncate,
};

static struct ram_node *new_node(const char *name)
{
	struct ram_node *n = kmalloc(sizeof(*n));
	if (n) {
		memset(n, 0, sizeof(*n));
		n->vnode.ops = &ram_ops;
		n->vnode.type = VNODE_DIR;
		n->vnode.refs = 1;
		strlcpy(n->name, name, sizeof(n->name));
	}
	return n;
}

struct vnode *ramfs_create(void)
{
	struct ram_node *root = new_node("");
	return root ? &root->vnode : 0;
}

/* Walks `path` from `root`, creating directories as needed, and returns
 * the node for the last element (or NULL on error, stored in *rc). */
static struct ram_node *make_path(struct vnode *root, const char *path, int *rc)
{
	struct ram_node *dir = node_of(root);
	while (*path) {
		char name[VFS_NAME_MAX + 1];
		size_t n = 0;
		while (path[n] && path[n] != '/') {
			if (n == VFS_NAME_MAX) {
				*rc = -ENAMETOOLONG;
				return 0;
			}
			name[n] = path[n];
			n++;
		}
		name[n] = '\0';
		path += n;
		if (*path == '/') {
			path++;
		}
		if (n == 0) {
			continue;
		}
		if (dir->vnode.type != VNODE_DIR) {
			*rc = -ENOTDIR;
			return 0;
		}

		struct ram_node **link = &dir->children;
		while (*link && strcmp((*link)->name, name) != 0) {
			link = &(*link)->next;
		}
		if (!*link) {
			*link = new_node(name);
			if (!*link) {
				*rc = -ENOMEM;
				return 0;
			}
		}
		dir = *link;
	}
	*rc = 0;
	return dir;
}

int ramfs_mkdir(struct vnode *root, const char *path)
{
	int rc;
	struct ram_node *n = make_path(root, path, &rc);
	return n && n->vnode.type != VNODE_DIR ? -EEXIST : rc;
}

int ramfs_add_file(struct vnode *root, const char *path, const void *data, uint32_t size)
{
	int rc;
	struct ram_node *n = make_path(root, path, &rc);
	if (!n) {
		return rc;
	}
	if (n->children || n == node_of(root)) {
		return -EEXIST;
	}
	n->vnode.type = VNODE_FILE;
	n->vnode.size = size;
	n->data = data;
	return 0;
}

int ramfs_add_mutable_file(struct vnode *root, const char *path)
{
	int rc;
	struct ram_node *n = make_path(root, path, &rc);
	if (!n) return rc;
	if (n->children || n == node_of(root)) return -EEXIST;
	n->vnode.type = VNODE_FILE;
	n->vnode.size = 0;
	n->data = 0;
	n->owned_data = 0;
	n->vnode.ops = &ram_writable_file_ops;
	return 0;
}

static void destroy(struct ram_node *n)
{
	while (n->children) {
		struct ram_node *c = n->children;
		n->children = c->next;
		destroy(c);
	}
	if (n->vnode.refs != 1) {
		panic("ramfs_destroy: a node is still referenced (vnode reference leak)");
	}
	if (n->owned_data) kfree(n->owned_data);
	kfree(n);
}

void ramfs_destroy(struct vnode *root)
{
	destroy(node_of(root));
}
