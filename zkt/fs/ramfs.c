#include "ramfs.h"
#include "heap.h"
#include "kerrno.h"
#include "kstring.h"
#include "panic.h"

struct ram_node {
	struct vnode vnode;
	char name[VFS_NAME_MAX + 1];
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
	out->type = VNODE_DIR;
	out->size = 0;
	return 1;
}

/* Nodes belong to their tree: the last reference only ever drops when
 * ramfs_destroy() releases the tree's own, which frees them directly. */
static const struct vnode_ops ram_ops = { .walk = ram_walk, .readdir = ram_readdir };

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

int ramfs_mkdir(struct vnode *root, const char *path)
{
	struct ram_node *dir = node_of(root);
	while (*path) {
		char name[VFS_NAME_MAX + 1];
		size_t n = 0;
		while (path[n] && path[n] != '/') {
			if (n == VFS_NAME_MAX) {
				return -ENAMETOOLONG;
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

		struct ram_node **link = &dir->children;
		while (*link && strcmp((*link)->name, name) != 0) {
			link = &(*link)->next;
		}
		if (!*link) {
			*link = new_node(name);
			if (!*link) {
				return -ENOMEM;
			}
		}
		dir = *link;
	}
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
	kfree(n);
}

void ramfs_destroy(struct vnode *root)
{
	destroy(node_of(root));
}
