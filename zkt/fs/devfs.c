#include "devfs.h"
#include "device.h"
#include "heap.h"
#include "kerrno.h"
#include "kstring.h"

struct dev_node {
	struct vnode vnode;
	struct device *dev;
};

static uint32_t device_bytes(struct device *d)
{
	if (d->class != DEVICE_BLOCK) {
		return d->char_ops && d->char_ops->size ? d->char_ops->size(d) : 0;
	}
	uint64_t bytes = (uint64_t)d->block_count * d->block_size;
	return bytes > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)bytes; /* 32-bit offsets */
}

/* Block devices through a one-block bounce buffer, so any byte range
 * can be read. */
static long read_block_bytes(struct device *d, uint32_t offset, uint8_t *buf, size_t len)
{
	uint32_t size = device_bytes(d);
	if (offset >= size) {
		return 0;
	}
	if (len > size - offset) {
		len = size - offset;
	}
	uint8_t *block = kmalloc(d->block_size);
	if (!block) {
		return -ENOMEM;
	}
	size_t done = 0;
	while (done < len) {
		uint32_t pos = offset + (uint32_t)done;
		uint32_t within = pos % d->block_size;
		int rc = device_read_blocks(d, pos / d->block_size, 1, block);
		if (rc) {
			kfree(block);
			return done ? (long)done : rc;
		}
		size_t chunk = d->block_size - within;
		if (chunk > len - done) {
			chunk = len - done;
		}
		memcpy(buf + done, block + within, chunk);
		done += chunk;
	}
	kfree(block);
	return (long)done;
}

static long dev_read(struct vnode *v, uint32_t offset, void *buf, size_t len)
{
	struct device *d = ((struct dev_node *)v)->dev;
	if (d->class == DEVICE_BLOCK) {
		return read_block_bytes(d, offset, buf, len);
	}
	return d->char_ops->pread ? d->char_ops->pread(d, offset, buf, len) : device_read(d, buf, len);
}

static long dev_write(struct vnode *v, uint32_t offset, const void *buf, size_t len)
{
	struct device *d = ((struct dev_node *)v)->dev;
	if (d->class == DEVICE_CHAR && d->char_ops->pwrite) {
		return d->char_ops->pwrite(d, offset, buf, len);
	}
	return device_write(d, buf, len);
}

static void dev_release(struct vnode *v)
{
	struct device *d = ((struct dev_node *)v)->dev;
	if (d->class == DEVICE_CHAR && d->char_ops->close) {
		d->char_ops->close(d);
	}
	kfree(v);
}

static int dev_poll(struct vnode *v)
{
	struct device *d = ((struct dev_node *)v)->dev;
	return d->class == DEVICE_CHAR && d->char_ops->poll ? d->char_ops->poll(d) : 1;
}

static const struct vnode_ops dev_ops = {
	.read = dev_read,
	.write = dev_write,
	.release = dev_release,
	.poll = dev_poll,
};

static int root_walk(struct vnode *dir, const char *name, struct vnode **out)
{
	(void)dir;
	struct device *d = device_find(name);
	if (!d) {
		return -ENOENT;
	}
	struct dev_node *n = kmalloc(sizeof(*n));
	if (!n) {
		return -ENOMEM;
	}
	n->vnode.ops = &dev_ops;
	n->vnode.type = VNODE_DEVICE;
	n->vnode.size = device_bytes(d);
	n->vnode.refs = 1;
	n->dev = d;
	if (d->class == DEVICE_CHAR && d->char_ops->open) {
		d->char_ops->open(d);
	}
	*out = &n->vnode;
	return 0;
}

static int root_readdir(struct vnode *dir, uint32_t index, struct dirent *out)
{
	(void)dir;
	struct device *d = device_next(0);
	while (d && index--) {
		d = device_next(d);
	}
	if (!d) {
		return 0;
	}
	strlcpy(out->name, d->name, sizeof(out->name));
	out->type = VNODE_DEVICE;
	out->size = device_bytes(d);
	return 1;
}

/* Static and never released: the permanent reference keeps it alive. */
static const struct vnode_ops root_ops = { .walk = root_walk, .readdir = root_readdir };
static struct vnode root = { .ops = &root_ops, .type = VNODE_DIR, .refs = 1 };

struct vnode *devfs_root(void)
{
	return &root;
}
