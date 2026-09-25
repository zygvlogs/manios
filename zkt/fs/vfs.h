/* Virtual filesystem: vnodes with 9P-shaped operations (walk one name,
 * read/write at an offset, list a directory), open files, and Plan 9
 * style bind/mount into the calling thread's namespace (namespace.h,
 * ADR-0003). Design notes: docs/milestones/M7-vfs-namespaces.md. */
#ifndef ZKT_FS_VFS_H
#define ZKT_FS_VFS_H

#include <stddef.h>
#include <stdint.h>

#define VFS_NAME_MAX 63
#define VFS_PATH_MAX 255
#define VFS_UNION_MAX 8 /* members of one union directory */

enum vnode_type {
	VNODE_DIR,
	VNODE_FILE,
	VNODE_DEVICE,
};

struct dirent {
	char name[VFS_NAME_MAX + 1];
	enum vnode_type type;
	uint32_t size;
};

struct vnode;

/* A NULL operation is unsupported. All may block. */
struct vnode_ops {
	/* Looks up `name` (never "." or "..": paths are cleaned lexically
	 * first) in directory `dir`; on success stores a new reference. */
	int (*walk)(struct vnode *dir, const char *name, struct vnode **out);
	/* Bytes read (0 at end of file), or a negated error. */
	long (*read)(struct vnode *v, uint32_t offset, void *buf, size_t len);
	long (*write)(struct vnode *v, uint32_t offset, const void *buf, size_t len);
	/* Fills the index'th entry: 1, or 0 past the last entry, or an error. */
	int (*readdir)(struct vnode *dir, uint32_t index, struct dirent *out);
	/* Called when the last reference goes; frees the vnode. */
	void (*release)(struct vnode *v);
};

/* Filesystems embed this in their own node structure. */
struct vnode {
	const struct vnode_ops *ops;
	enum vnode_type type;
	uint32_t size;
	uint32_t refs;
};

void vnode_ref(struct vnode *v);
void vnode_unref(struct vnode *v);

/* Open files. Paths are absolute and resolved in the calling thread's
 * namespace. A directory opened on a union reads the entries of every
 * member, in union order, duplicates included (as in Plan 9). `mode` is
 * OREAD, OWRITE or ORDWR (zkt_abi.h); asking to write something that
 * can't be written fails at open (EISDIR, EROFS). */
struct file;
int vfs_open(const char *path, int mode, struct file **out);
long vfs_read(struct file *f, void *buf, size_t len);
long vfs_write(struct file *f, const void *buf, size_t len);
/* Moves the file's offset (SEEK_SET/CUR/END, zkt_abi.h); returns it.
 * A directory can only be rewound. Shared by vfs_dup copies. */
long vfs_seek(struct file *f, int32_t offset, int whence);
/* At an explicit offset, leaving the file's own offset alone. */
long vfs_pread(struct file *f, void *buf, size_t len, uint32_t offset);
long vfs_pwrite(struct file *f, const void *buf, size_t len, uint32_t offset);
/* Another reference to the same open file (sharing its offset). */
struct file *vfs_dup(struct file *f);
/* The last element of the path it was opened by ("/" for the root). */
const char *vfs_name(const struct file *f);
int vfs_readdir(struct file *f, struct dirent *out); /* 1, 0 at end, or error */
/* Entry number `entry` of the listing (from 0): continues from the last
 * one read, or starts over when asked for an earlier one. */
int vfs_readdir_at(struct file *f, uint32_t entry, struct dirent *out);
enum vnode_type vfs_type(const struct file *f);
uint32_t vfs_size(const struct file *f);
/* Drops a reference; the file closes with the last one. */
void vfs_close(struct file *f);

/* Namespace changes in the calling thread's namespace. Like Plan 9's
 * bind(2): `old` then resolves to `new` (BIND_REPLACE), or to a union of
 * both with `new` searched first (BIND_BEFORE, "bind -b") or last
 * (BIND_AFTER, "bind -a"). Directories bind only onto directories. */
enum bind_flag {
	BIND_REPLACE,
	BIND_BEFORE,
	BIND_AFTER,
};
int vfs_bind(const char *new_path, const char *old_path, enum bind_flag flag);
/* As vfs_bind, with a filesystem root instead of a path; `label` names
 * it in the mount table listing. */
int vfs_mount(struct vnode *root, const char *label, const char *old_path, enum bind_flag flag);
/* Drops every bind on `old_path`. */
int vfs_unbind(const char *old_path);

#endif
