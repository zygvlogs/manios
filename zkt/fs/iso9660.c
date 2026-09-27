/* Read-only ISO 9660, written from ECMA-119 (the free edition of ISO
 * 9660), with the long names of Rock Ridge (IEEE P1282, over the System
 * Use Sharing Protocol, P1281) and Joliet (Microsoft's published
 * specification: UCS-2 names in a second directory tree, reached by a
 * Supplementary Volume Descriptor).
 *
 * Names come from Rock Ridge's NM entries when the disc has Rock Ridge
 * (the System Use area of its root's "." record starts with SP), else
 * from the Joliet tree when there is one, else they are the ISO names:
 * upper case with a version (";1"), shown in lower case without it. ISO
 * and Joliet names are found whatever their case; Rock Ridge's, which
 * are Unix names, as they are.
 *
 * A file is one extent: contiguous blocks from its record's address.
 * Whole blocks are read straight into the caller's buffer; the rest
 * through a one-block cache. */
#include "iso9660.h"
#include <stdbool.h>
#include <stdint.h>
#include "cpu.h"
#include "heap.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "mutex.h"

#define ISO_BLOCK 2048
#define FIRST_DESCRIPTOR 16
#define MAX_DESCRIPTORS 32
#define DESC_PRIMARY 1
#define DESC_SUPPLEMENTARY 2
#define DESC_TERMINATOR 255
#define ROOT_RECORD 156 /* in a volume descriptor */
#define MAX_BLOCKS_PER_READ 32

#define FLAG_DIRECTORY  0x02
#define FLAG_ASSOCIATED 0x04

enum names { PLAIN, JOLIET, ROCK_RIDGE };

struct iso_fs {
	struct device *dev;
	uint32_t refs;     /* its nodes, and a removable root */
	uint32_t changes;  /* the device's media_changes when mounted */
	struct mutex lock; /* the block cache */
	enum names names;
	uint8_t susp_skip; /* SP's bytes to skip at each System Use area */
	uint32_t root_extent, root_bytes, volume_blocks;
	char label[33];
	uint32_t cached;
	bool cache_valid;
	uint8_t cache[ISO_BLOCK];
};

struct iso_node {
	struct vnode vnode;
	struct iso_fs *fs;        /* NULL: a removable root with no disc */
	uint32_t extent, bytes;   /* where its data is, and how much */
	struct device *removable; /* set on a removable drive's root */
};

/* A directory record, decoded. */
struct record {
	uint32_t extent, bytes;
	bool dir;
	char name[VFS_NAME_MAX + 1];
};

static struct mutex follow_lock = MUTEX_INIT; /* removable roots changing discs */

static uint16_t le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void fs_get(struct iso_fs *fs)
{
	uint32_t flags = cpu_irq_save();
	fs->refs++;
	cpu_irq_restore(flags);
}

static void fs_put(struct iso_fs *fs)
{
	uint32_t flags = cpu_irq_save();
	bool last = --fs->refs == 0;
	cpu_irq_restore(flags);
	if (last) {
		kfree(fs);
	}
}

/* A disc taken out or changed since the mount. */
static bool stale(const struct iso_fs *fs)
{
	return fs->dev->media_changes != fs->changes;
}

/* Caller holds fs->lock. NULL on an I/O error or a changed disc. */
static const uint8_t *read_block(struct iso_fs *fs, uint32_t lba)
{
	if (stale(fs)) {
		return 0;
	}
	if (fs->cache_valid && fs->cached == lba) {
		return fs->cache;
	}
	uint32_t per = ISO_BLOCK / fs->dev->block_size;
	fs->cache_valid = false;
	if (lba >= fs->dev->block_count / per
	    || device_read_blocks(fs->dev, lba * per, per, fs->cache) != 0) {
		return 0;
	}
	fs->cached = lba;
	fs->cache_valid = true;
	return fs->cache;
}

static char lower(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

/* "README.TXT;1" -> "readme.txt"; "MAKEFILE.;1" -> "makefile". */
static void plain_name(const uint8_t *name, uint8_t len, char *out)
{
	size_t n = 0;
	for (uint8_t i = 0; i < len && name[i] != ';' && n < VFS_NAME_MAX; i++) {
		out[n++] = lower((char)name[i]);
	}
	while (n > 0 && out[n - 1] == '.') {
		n--;
	}
	out[n] = '\0';
}

/* UCS-2, big-endian, to UTF-8, without the ";1". */
static void joliet_name(const uint8_t *name, uint8_t len, char *out)
{
	size_t n = 0;
	for (uint8_t i = 0; i + 1 < len; i += 2) {
		uint32_t c = (uint32_t)name[i] << 8 | name[i + 1];
		if (c == ';') {
			break;
		}
		if (c < 0x80 && n + 1 <= VFS_NAME_MAX) {
			out[n++] = (char)c;
		} else if (c < 0x800 && n + 2 <= VFS_NAME_MAX) {
			out[n++] = (char)(0xC0 | c >> 6);
			out[n++] = (char)(0x80 | (c & 0x3F));
		} else if (c >= 0x800 && n + 3 <= VFS_NAME_MAX) {
			out[n++] = (char)(0xE0 | c >> 12);
			out[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
			out[n++] = (char)(0x80 | (c & 0x3F));
		} else {
			break;
		}
	}
	out[n] = '\0';
}

/* Rock Ridge: the name the NM entries of a System Use area (and its
 * continuation areas, CE) spell. False if the record is a directory
 * Rock Ridge moved (RE): it is shown where it belongs, not here.
 * Caller holds fs->lock. */
static bool rock_ridge_name(struct iso_fs *fs, const uint8_t *su, uint32_t len, char *out)
{
	size_t n = 0;
	bool relocated = false;
	uint8_t *area = 0;
	uint32_t ce_block = 0, ce_offset = 0, ce_len = 0;

	for (int areas = 0; areas < 8; areas++) {
		while (len >= 4) {
			uint32_t elen = su[2];
			if (elen < 4 || elen > len) {
				break;
			}
			if (su[0] == 'N' && su[1] == 'M' && elen >= 5 && !(su[4] & 0x06)) {
				for (uint32_t i = 5; i < elen && n < VFS_NAME_MAX; i++) {
					out[n++] = (char)su[i];
				}
			} else if (su[0] == 'R' && su[1] == 'E') {
				relocated = true;
			} else if (su[0] == 'C' && su[1] == 'E' && elen >= 28) {
				ce_block = le32(su + 4);
				ce_offset = le32(su + 12);
				ce_len = le32(su + 20);
			} else if (su[0] == 'S' && su[1] == 'T') {
				break;
			}
			su += elen;
			len -= elen;
		}
		if (!ce_len || ce_offset + ce_len > ISO_BLOCK || (!area && !(area = kmalloc(ISO_BLOCK)))) {
			break;
		}
		const uint8_t *b = read_block(fs, ce_block);
		if (!b) {
			break;
		}
		memcpy(area, b + ce_offset, ce_len);
		su = area;
		len = ce_len;
		ce_len = 0;
	}
	kfree(area);
	out[n] = '\0';
	return !relocated;
}

/* False for records not shown: ".", "..", associated files, moved
 * directories, nameless ones. Caller holds fs->lock. */
static bool decode(struct iso_fs *fs, const uint8_t *rec, uint32_t len, struct record *r)
{
	uint8_t name_len = rec[32];
	const uint8_t *name = rec + 33;
	if (33u + name_len > len || (name_len == 1 && name[0] <= 1) || (rec[25] & FLAG_ASSOCIATED)) {
		return false;
	}
	r->extent = le32(rec + 2) + rec[1]; /* after an extended attribute record */
	r->bytes = le32(rec + 10);
	r->dir = rec[25] & FLAG_DIRECTORY;
	r->name[0] = '\0';
	if (fs->names == ROCK_RIDGE) {
		uint32_t su = 33u + name_len + (name_len % 2 ? 0 : 1) + fs->susp_skip;
		if (!rock_ridge_name(fs, rec + su, su < len ? len - su : 0, r->name)) {
			return false;
		}
	}
	if (!r->name[0]) {
		if (fs->names == JOLIET) {
			joliet_name(name, name_len, r->name);
		} else {
			plain_name(name, name_len, r->name);
		}
	}
	return r->name[0] != '\0';
}

/* Visits a directory's records; fn returns true to stop. 0 or -EIO.
 * Caller holds fs->lock. Records don't cross blocks: a zero length
 * byte means the rest of the block is unused. */
static int for_each_record(struct iso_fs *fs, uint32_t extent, uint32_t bytes,
                           bool (*fn)(const struct record *r, void *ctx), void *ctx)
{
	uint8_t rec[256];
	struct record r;
	for (uint32_t off = 0; off < bytes;) {
		const uint8_t *b = read_block(fs, extent + off / ISO_BLOCK);
		if (!b) {
			return -EIO;
		}
		uint32_t within = off % ISO_BLOCK, len = b[within];
		if (len == 0) {
			off = (off / ISO_BLOCK + 1) * ISO_BLOCK;
			continue;
		}
		if (len < 34 || within + len > ISO_BLOCK) {
			return -EIO;
		}
		memcpy(rec, b + within, len); /* the cache may be reused for CE areas */
		off += len;
		if (decode(fs, rec, len, &r) && fn(&r, ctx)) {
			return 0;
		}
	}
	return 0;
}

static const struct vnode_ops iso_ops;

static struct iso_node *new_node(struct iso_fs *fs, uint32_t extent, uint32_t bytes, bool dir)
{
	struct iso_node *n = kmalloc(sizeof(*n));
	if (n) {
		memset(n, 0, sizeof(*n));
		n->fs = fs;
		if (fs) {
			fs_get(fs);
		}
		n->extent = extent;
		n->bytes = bytes;
		n->vnode.ops = &iso_ops;
		n->vnode.type = dir ? VNODE_DIR : VNODE_FILE;
		n->vnode.size = dir ? 0 : bytes;
		n->vnode.refs = 1;
	}
	return n;
}

/* Reads the volume descriptors: a new file system, or -EINVAL. */
static int open_fs(struct device *dev, struct iso_fs **out)
{
	if (dev->class != DEVICE_BLOCK || (dev->block_size != ISO_BLOCK && dev->block_size != 512)) {
		return -EINVAL;
	}
	struct iso_fs *fs = kmalloc(sizeof(*fs));
	if (!fs) {
		return -ENOMEM;
	}
	memset(fs, 0, sizeof(*fs));
	fs->dev = dev;
	fs->refs = 1;
	fs->changes = dev->media_changes;
	fs->lock = (struct mutex)MUTEX_INIT;

	bool primary = false;
	uint32_t joliet_extent = 0, joliet_bytes = 0;
	for (uint32_t i = 0; i < MAX_DESCRIPTORS; i++) {
		const uint8_t *b = read_block(fs, FIRST_DESCRIPTOR + i);
		if (!b || memcmp(b + 1, "CD001", 5) != 0 || b[0] == DESC_TERMINATOR) {
			break;
		}
		if (b[0] == DESC_PRIMARY && !primary && le16(b + 128) == ISO_BLOCK) {
			primary = true;
			fs->root_extent = le32(b + ROOT_RECORD + 2);
			fs->root_bytes = le32(b + ROOT_RECORD + 10);
			fs->volume_blocks = le32(b + 80);
			memcpy(fs->label, b + 40, 32);
			int n = 32;
			while (n > 0 && fs->label[n - 1] == ' ') {
				n--;
			}
			fs->label[n] = '\0';
		} else if (b[0] == DESC_SUPPLEMENTARY && b[88] == '%' && b[89] == '/'
		           && (b[90] == '@' || b[90] == 'C' || b[90] == 'E')) {
			joliet_extent = le32(b + ROOT_RECORD + 2);
			joliet_bytes = le32(b + ROOT_RECORD + 10);
		}
	}
	if (!primary) {
		kfree(fs);
		return -EINVAL;
	}
	/* Rock Ridge: the root's "." record (name length 1, so no padding)
	 * has SP first in its System Use area, at byte 34. */
	const uint8_t *b = read_block(fs, fs->root_extent);
	if (b && b[0] >= 41 && b[32] == 1 && b[34] == 'S' && b[35] == 'P' && b[36] >= 7
	    && b[38] == 0xBE && b[39] == 0xEF) {
		fs->names = ROCK_RIDGE;
		fs->susp_skip = b[40];
	} else if (joliet_extent) {
		fs->names = JOLIET;
		fs->root_extent = joliet_extent;
		fs->root_bytes = joliet_bytes;
	}
	*out = fs;
	return 0;
}

static void describe(struct iso_fs *fs, char *desc, size_t desc_len)
{
	static const char *const NAMES[] = { "ISO names", "Joliet", "Rock Ridge" };
	ksnprintf(desc, desc_len, "ISO 9660, %s, %lu MiB%s%s", NAMES[fs->names],
	          (fs->volume_blocks + 511) / 512, fs->label[0] ? ", " : "", fs->label);
}

/* A removable root takes up the disc in the drive now. Caller holds
 * follow_lock. */
static void follow_disc(struct iso_node *root)
{
	struct device *dev = root->removable;
	dev->block_ops->check_media(dev);
	if (root->fs && !stale(root->fs)) {
		return;
	}
	if (root->fs) {
		fs_put(root->fs);
		root->fs = 0;
	}
	struct iso_fs *fs;
	if (dev->block_count && open_fs(dev, &fs) == 0) {
		char desc[64];
		describe(fs, desc, sizeof(desc));
		kprintf("%s: %s\n", dev->name, desc);
		root->fs = fs; /* open_fs's reference */
		root->extent = fs->root_extent;
		root->bytes = fs->root_bytes;
	}
}

/* The file system and directory to look in: n's own, or for a removable
 * root the disc's in the drive now; with a reference to drop. NULL: no
 * disc. */
static struct iso_fs *directory(struct iso_node *n, uint32_t *extent, uint32_t *bytes)
{
	if (n->removable) {
		mutex_lock(&follow_lock);
		follow_disc(n);
	}
	struct iso_fs *fs = n->fs;
	if (fs) {
		fs_get(fs);
		*extent = n->extent;
		*bytes = n->bytes;
	}
	if (n->removable) {
		mutex_unlock(&follow_lock);
	}
	return fs;
}

struct walk_ctx {
	const char *want;
	bool exact;
	struct record found;
	bool hit;
};

static bool names_equal(const char *a, const char *b, bool exact)
{
	for (; *a && *b; a++, b++) {
		if (exact ? *a != *b : lower(*a) != lower(*b)) {
			return false;
		}
	}
	return *a == *b;
}

static bool match(const struct record *r, void *ctx)
{
	struct walk_ctx *w = ctx;
	if (names_equal(r->name, w->want, w->exact)) {
		w->found = *r;
		w->hit = true;
		return true;
	}
	return false;
}

static int iso_walk(struct vnode *dir, const char *name, struct vnode **out)
{
	uint32_t extent, bytes;
	struct iso_fs *fs = directory((struct iso_node *)dir, &extent, &bytes);
	if (!fs) {
		return -ENOENT;
	}
	struct walk_ctx *w = kmalloc(sizeof(*w));
	if (!w) {
		fs_put(fs);
		return -ENOMEM;
	}
	w->want = name;
	w->exact = fs->names == ROCK_RIDGE;
	w->hit = false;
	mutex_lock(&fs->lock);
	int rc = for_each_record(fs, extent, bytes, match, w);
	mutex_unlock(&fs->lock);
	if (rc == 0 && !w->hit) {
		rc = -ENOENT;
	}
	if (rc == 0) {
		struct iso_node *n = new_node(fs, w->found.extent, w->found.bytes, w->found.dir);
		if (n) {
			*out = &n->vnode;
		} else {
			rc = -ENOMEM;
		}
	}
	kfree(w);
	fs_put(fs);
	return rc;
}

struct readdir_ctx {
	uint32_t skip;
	struct dirent *out;
	bool hit;
};

static bool nth(const struct record *r, void *ctx)
{
	struct readdir_ctx *c = ctx;
	if (c->skip--) {
		return false;
	}
	strlcpy(c->out->name, r->name, sizeof(c->out->name));
	c->out->type = r->dir ? VNODE_DIR : VNODE_FILE;
	c->out->size = r->dir ? 0 : r->bytes;
	c->hit = true;
	return true;
}

static int iso_readdir(struct vnode *dir, uint32_t index, struct dirent *out)
{
	uint32_t extent, bytes;
	struct iso_fs *fs = directory((struct iso_node *)dir, &extent, &bytes);
	if (!fs) {
		return 0; /* no disc: empty */
	}
	struct readdir_ctx c = { .skip = index, .out = out, .hit = false };
	mutex_lock(&fs->lock);
	int rc = for_each_record(fs, extent, bytes, nth, &c);
	mutex_unlock(&fs->lock);
	fs_put(fs);
	return rc ? rc : c.hit;
}

static long iso_read(struct vnode *v, uint32_t offset, void *buf, size_t len)
{
	struct iso_node *n = (struct iso_node *)v;
	struct iso_fs *fs = n->fs;
	uint8_t *out = buf;
	if (!fs || v->type != VNODE_FILE || offset >= n->bytes) {
		return 0;
	}
	if (len > n->bytes - offset) {
		len = n->bytes - offset;
	}
	uint32_t per = ISO_BLOCK / fs->dev->block_size;
	size_t done = 0;
	mutex_lock(&fs->lock);
	while (done < len) {
		uint32_t pos = offset + (uint32_t)done;
		uint32_t lba = n->extent + pos / ISO_BLOCK, within = pos % ISO_BLOCK;
		if (within == 0 && len - done >= ISO_BLOCK) {
			uint32_t blocks = (uint32_t)((len - done) / ISO_BLOCK);
			blocks = blocks < MAX_BLOCKS_PER_READ ? blocks : MAX_BLOCKS_PER_READ;
			if (stale(fs) || device_read_blocks(fs->dev, lba * per, blocks * per, out + done) != 0) {
				break;
			}
			done += blocks * ISO_BLOCK;
			continue;
		}
		const uint8_t *b = read_block(fs, lba);
		if (!b) {
			break;
		}
		size_t chunk = ISO_BLOCK - within < len - done ? ISO_BLOCK - within : len - done;
		memcpy(out + done, b + within, chunk);
		done += chunk;
	}
	mutex_unlock(&fs->lock);
	return done ? (long)done : -EIO;
}

static void iso_release(struct vnode *v)
{
	struct iso_node *n = (struct iso_node *)v;
	if (n->fs) {
		fs_put(n->fs);
	}
	kfree(n);
}

static const struct vnode_ops iso_ops = {
	.walk = iso_walk,
	.read = iso_read,
	.readdir = iso_readdir,
	.release = iso_release,
};

int iso9660_mount(struct device *dev, struct vnode **root, char *desc, size_t desc_len)
{
	struct iso_fs *fs;
	int rc = open_fs(dev, &fs);
	if (rc) {
		return rc;
	}
	struct iso_node *n = new_node(fs, fs->root_extent, fs->root_bytes, true);
	fs_put(fs); /* the root's reference keeps it */
	if (!n) {
		return -ENOMEM;
	}
	describe(fs, desc, desc_len);
	*root = &n->vnode;
	return 0;
}

int iso9660_mount_removable(struct device *dev, struct vnode **root)
{
	struct iso_node *n = new_node(0, 0, 0, true);
	if (!n) {
		return -ENOMEM;
	}
	n->removable = dev;
	mutex_lock(&follow_lock);
	follow_disc(n); /* says what is in the drive */
	mutex_unlock(&follow_lock);
	*root = &n->vnode;
	return 0;
}
