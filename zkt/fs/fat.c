/* Read-only FAT12/16, written from Microsoft's published FAT
 * specification ("FAT: General Overview of On-Disk Format"). */
#include "fat.h"
#include <stdbool.h>
#include <stdint.h>
#include "heap.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "mutex.h"

#define SECTOR 512
#define DIRENT_SIZE 32

#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_LONG_NAME 0x0F

#define ENTRY_END     0x00
#define ENTRY_DELETED 0xE5
#define ENTRY_KANJI_E5 0x05 /* first byte really is 0xE5 */

struct fat_fs {
	struct device *dev;
	struct mutex lock; /* the sector cache and node cursors */
	bool fat12;
	uint32_t sectors_per_cluster;
	uint32_t bytes_per_cluster;
	uint32_t fat_start;     /* first sector of the first FAT */
	uint32_t root_start;    /* fixed root directory region (FAT12/16) */
	uint32_t root_bytes;
	uint32_t data_start;    /* cluster 2 */
	uint32_t cluster_count; /* valid clusters are 2..cluster_count+1 */
	uint32_t cached_sector;
	bool cache_valid;
	uint8_t cache[SECTOR];
};

struct fat_node {
	struct vnode vnode;
	struct fat_fs *fs;
	bool fixed_root;        /* FAT12/16 root: a fixed region, not a chain */
	uint32_t first_cluster; /* 0 for empty files */
	uint32_t cursor_index;  /* last chain position looked up, so reading */
	uint32_t cursor_cluster; /* sequentially doesn't rewalk the chain */
};

static uint16_t le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Caller holds fs->lock. NULL on I/O error. */
static const uint8_t *read_sector(struct fat_fs *fs, uint32_t sector)
{
	if (!fs->cache_valid || fs->cached_sector != sector) {
		fs->cache_valid = false;
		if (device_read_blocks(fs->dev, sector, 1, fs->cache) != 0) {
			return 0;
		}
		fs->cached_sector = sector;
		fs->cache_valid = true;
	}
	return fs->cache;
}

static int fat_byte(struct fat_fs *fs, uint32_t offset)
{
	const uint8_t *s = read_sector(fs, fs->fat_start + offset / SECTOR);
	return s ? s[offset % SECTOR] : -EIO;
}

/* The FAT entry for `cluster`: the next cluster in its chain, or an
 * end-of-chain / bad-cluster marker. FAT12 packs two 12-bit entries in
 * three bytes, so an entry can straddle a sector boundary. */
static int fat_entry(struct fat_fs *fs, uint32_t cluster, uint32_t *out)
{
	uint32_t offset = fs->fat12 ? cluster + cluster / 2 : cluster * 2;
	int lo = fat_byte(fs, offset);
	int hi = fat_byte(fs, offset + 1);
	if (lo < 0 || hi < 0) {
		return -EIO;
	}
	uint32_t v = (uint32_t)lo | (uint32_t)hi << 8;
	if (fs->fat12) {
		v = (cluster & 1) ? v >> 4 : v & 0xFFF;
	}
	*out = v;
	return 0;
}

static bool is_end_of_chain(struct fat_fs *fs, uint32_t v)
{
	return v >= (fs->fat12 ? 0xFF8u : 0xFFF8u);
}

static bool valid_cluster(struct fat_fs *fs, uint32_t c)
{
	return c >= 2 && c < fs->cluster_count + 2;
}

/* The index'th cluster of a node's chain: 0 past its end. Chains are
 * followed at most cluster_count steps, so a looping chain on a corrupt
 * disk reports -EIO instead of hanging. */
static int cluster_at(struct fat_node *n, uint32_t index, uint32_t *out)
{
	struct fat_fs *fs = n->fs;
	uint32_t i = 0;
	uint32_t c = n->first_cluster;
	if (n->cursor_cluster && n->cursor_index <= index) {
		i = n->cursor_index;
		c = n->cursor_cluster;
	}
	if (!valid_cluster(fs, c)) {
		*out = 0;
		return c == 0 ? 0 : -EIO;
	}
	for (; i < index; i++) {
		if (index - i > fs->cluster_count) {
			return -EIO;
		}
		uint32_t next;
		if (fat_entry(fs, c, &next) != 0) {
			return -EIO;
		}
		if (is_end_of_chain(fs, next)) {
			*out = 0;
			return 0;
		}
		if (!valid_cluster(fs, next)) {
			return -EIO;
		}
		c = next;
	}
	n->cursor_index = index;
	n->cursor_cluster = c;
	*out = c;
	return 0;
}

/* Reads a node's raw contents (file data or directory entries); stops
 * at the end of the chain or of the fixed root region. Caller holds
 * the lock. */
static long node_read(struct fat_node *n, uint32_t offset, uint8_t *buf, uint32_t len)
{
	struct fat_fs *fs = n->fs;
	uint32_t done = 0;

	while (done < len) {
		uint32_t sector;
		if (n->fixed_root) {
			if (offset >= fs->root_bytes) {
				break;
			}
			sector = fs->root_start + offset / SECTOR;
		} else {
			uint32_t c;
			if (cluster_at(n, offset / fs->bytes_per_cluster, &c) != 0) {
				return done ? (long)done : -EIO;
			}
			if (!c) {
				break;
			}
			sector = fs->data_start + (c - 2) * fs->sectors_per_cluster
			         + (offset % fs->bytes_per_cluster) / SECTOR;
		}
		const uint8_t *s = read_sector(fs, sector);
		if (!s) {
			return done ? (long)done : -EIO;
		}
		uint32_t within = offset % SECTOR;
		uint32_t chunk = SECTOR - within < len - done ? SECTOR - within : len - done;
		memcpy(buf + done, s + within, chunk);
		done += chunk;
		offset += chunk;
	}
	return (long)done;
}

static long fat_read(struct vnode *v, uint32_t offset, void *buf, size_t len)
{
	struct fat_node *n = (struct fat_node *)v;
	if (offset >= v->size) {
		return 0;
	}
	if (len > v->size - offset) {
		len = v->size - offset;
	}
	mutex_lock(&n->fs->lock);
	long rc = node_read(n, offset, buf, (uint32_t)len);
	mutex_unlock(&n->fs->lock);
	return rc;
}

/* "README  TXT" -> "readme.txt". */
static void short_name(const uint8_t *e, char *out)
{
	size_t len = 0;
	for (int i = 0; i < 8 && e[i] != ' '; i++) {
		char c = (char)((i == 0 && e[0] == ENTRY_KANJI_E5) ? 0xE5 : e[i]);
		out[len++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
	}
	if (e[8] != ' ') {
		out[len++] = '.';
		for (int i = 8; i < 11 && e[i] != ' '; i++) {
			char c = (char)e[i];
			out[len++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
		}
	}
	out[len] = '\0';
}

static bool names_equal_nocase(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
		char y = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
		if (x != y) {
			return false;
		}
	}
	return *a == *b;
}

/* Visits the visible entries of directory `n` (not deleted, long-name,
 * volume-label, "." or ".." entries). fn returns true to stop.
 * Caller holds the lock. Returns 0 or -EIO. */
static int for_each_entry(struct fat_node *n,
                          bool (*fn)(const uint8_t *e, const char *name, void *ctx),
                          void *ctx)
{
	uint8_t e[DIRENT_SIZE];
	for (uint32_t off = 0;; off += DIRENT_SIZE) {
		long got = node_read(n, off, e, DIRENT_SIZE);
		if (got < 0) {
			return -EIO;
		}
		if (got < DIRENT_SIZE || e[0] == ENTRY_END) {
			return 0;
		}
		if (e[0] == ENTRY_DELETED || e[11] == ATTR_LONG_NAME || (e[11] & ATTR_VOLUME_ID)
		    || e[0] == '.') {
			continue;
		}
		char name[13];
		short_name(e, name);
		if (fn(e, name, ctx)) {
			return 0;
		}
	}
}

static const struct vnode_ops fat_ops;

static struct fat_node *new_node(struct fat_fs *fs, const uint8_t *e)
{
	struct fat_node *n = kmalloc(sizeof(*n));
	if (n) {
		memset(n, 0, sizeof(*n));
		n->fs = fs;
		n->first_cluster = le16(e + 26);
		n->vnode.ops = &fat_ops;
		n->vnode.type = (e[11] & ATTR_DIRECTORY) ? VNODE_DIR : VNODE_FILE;
		n->vnode.size = (e[11] & ATTR_DIRECTORY) ? 0 : le32(e + 28);
		n->vnode.refs = 1;
	}
	return n;
}

struct walk_ctx {
	const char *want;
	uint8_t found[DIRENT_SIZE];
	bool hit;
};

static bool match_name(const uint8_t *e, const char *name, void *ctx)
{
	struct walk_ctx *w = ctx;
	if (names_equal_nocase(name, w->want)) {
		memcpy(w->found, e, DIRENT_SIZE);
		w->hit = true;
		return true;
	}
	return false;
}

static int fat_walk(struct vnode *dir, const char *name, struct vnode **out)
{
	struct fat_node *d = (struct fat_node *)dir;
	struct walk_ctx w = { .want = name, .hit = false };

	mutex_lock(&d->fs->lock);
	int rc = for_each_entry(d, match_name, &w);
	mutex_unlock(&d->fs->lock);
	if (rc) {
		return rc;
	}
	if (!w.hit) {
		return -ENOENT;
	}
	struct fat_node *n = new_node(d->fs, w.found);
	if (!n) {
		return -ENOMEM;
	}
	*out = &n->vnode;
	return 0;
}

struct readdir_ctx {
	uint32_t skip;
	struct dirent *out;
	bool hit;
};

static bool nth_entry(const uint8_t *e, const char *name, void *ctx)
{
	struct readdir_ctx *r = ctx;
	if (r->skip--) {
		return false;
	}
	strlcpy(r->out->name, name, sizeof(r->out->name));
	r->out->type = (e[11] & ATTR_DIRECTORY) ? VNODE_DIR : VNODE_FILE;
	r->out->size = (e[11] & ATTR_DIRECTORY) ? 0 : le32(e + 28);
	r->hit = true;
	return true;
}

/* Rescans from the start for each index: O(n^2) per listing, which is
 * fine for directories of the sizes FAT12/16 volumes hold. */
static int fat_readdir(struct vnode *dir, uint32_t index, struct dirent *out)
{
	struct fat_node *d = (struct fat_node *)dir;
	struct readdir_ctx r = { .skip = index, .out = out, .hit = false };

	mutex_lock(&d->fs->lock);
	int rc = for_each_entry(d, nth_entry, &r);
	mutex_unlock(&d->fs->lock);
	return rc ? rc : r.hit;
}

static void fat_release(struct vnode *v)
{
	kfree(v);
}

static const struct vnode_ops fat_ops = {
	.walk = fat_walk,
	.read = fat_read,
	.readdir = fat_readdir,
	.release = fat_release,
};

int fat_mount(struct device *dev, struct vnode **root, char *desc, size_t desc_len)
{
	if (dev->class != DEVICE_BLOCK || dev->block_size != SECTOR) {
		return -EINVAL;
	}
	uint8_t *b = kmalloc(SECTOR);
	if (!b) {
		return -ENOMEM;
	}
	if (device_read_blocks(dev, 0, 1, b) != 0) {
		kfree(b);
		return -EIO;
	}

	uint32_t bytes_per_sector = le16(b + 11);
	uint32_t per_cluster = b[13];
	uint32_t reserved = le16(b + 14);
	uint32_t fats = b[16];
	uint32_t root_entries = le16(b + 17);
	uint32_t total = le16(b + 19) ? le16(b + 19) : le32(b + 32);
	uint32_t fat_size = le16(b + 22); /* 0 on FAT32 */
	uint8_t media = b[21];
	uint16_t signature = le16(b + 510);
	kfree(b);

	if (signature != 0xAA55 || bytes_per_sector != SECTOR || !per_cluster
	    || (per_cluster & (per_cluster - 1)) || reserved == 0 || fats == 0 || fats > 2
	    || (media != 0xF0 && media < 0xF8) || root_entries == 0 || fat_size == 0
	    || total > dev->block_count) {
		return -EINVAL;
	}
	uint32_t root_sectors = (root_entries * DIRENT_SIZE + SECTOR - 1) / SECTOR;
	uint32_t data_start = reserved + fats * fat_size + root_sectors;
	if (data_start >= total) {
		return -EINVAL;
	}
	uint32_t clusters = (total - data_start) / per_cluster;
	if (clusters >= 65525) {
		return -EINVAL; /* FAT32 */
	}

	struct fat_fs *fs = kmalloc(sizeof(*fs));
	if (!fs) {
		return -ENOMEM;
	}
	memset(fs, 0, sizeof(*fs));
	fs->dev = dev;
	fs->lock = (struct mutex)MUTEX_INIT;
	fs->fat12 = clusters < 4085; /* the spec's rule: by cluster count alone */
	fs->sectors_per_cluster = per_cluster;
	fs->bytes_per_cluster = per_cluster * SECTOR;
	fs->fat_start = reserved;
	fs->root_start = reserved + fats * fat_size;
	fs->root_bytes = root_entries * DIRENT_SIZE;
	fs->data_start = data_start;
	fs->cluster_count = clusters;

	struct fat_node *r = kmalloc(sizeof(*r));
	if (!r) {
		kfree(fs);
		return -ENOMEM;
	}
	memset(r, 0, sizeof(*r));
	r->fs = fs;
	r->fixed_root = true;
	r->vnode.ops = &fat_ops;
	r->vnode.type = VNODE_DIR;
	r->vnode.refs = 1;
	*root = &r->vnode;
	ksnprintf(desc, desc_len, "%s, %lu MiB", fs->fat12 ? "FAT12" : "FAT16", total / 2048);
	return 0;
}
