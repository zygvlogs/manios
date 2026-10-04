/* FAT12/16, read from Microsoft's published FAT specification ("FAT:
 * General Overview of On-Disk Format"), and written to it since M22: a
 * file's clusters are allocated and linked, directory entries are
 * created, emptied, renamed and kept right, and every sector reaches
 * the disk before the call returns (there is no dirty cache to flush).
 * A disk whose device takes no writes is mounted read-only: its nodes
 * then get fat_ro_ops, so opening one for writing, or creating
 * anything, is EROFS -- as it was everywhere in ManiOS before M22.
 *
 * Names are 8.3. A name that doesn't fit is kept under its numeric
 * alias ("MANIOS~1.TXT"), which is what FAT does with a long name it
 * has no room to record; long-name entries found on the disk are
 * skipped when reading and dropped when their entry is changed. */
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
#define ATTR_ARCHIVE   0x20

#define ENTRY_END     0x00
#define ENTRY_DELETED 0xE5
#define ENTRY_KANJI_E5 0x05 /* first byte really is 0xE5 */

/* What everything created here is dated: 1 January 1980, the FAT
 * epoch. ManiOS keeps no file times yet (docs/dos.md), so a date is
 * better than none. */
#define FAT_DATE_EPOCH 0x0021

struct fat_fs {
	struct device *dev;
	struct mutex lock; /* the sector cache and node cursors */
	bool fat12;
	bool read_only;         /* the device takes no writes */
	uint32_t fats;          /* copies of the FAT on disk: 1 or 2 */
	uint32_t sectors_per_cluster;
	uint32_t bytes_per_cluster;
	uint32_t fat_start;     /* first sector of the first FAT */
	uint32_t fat_size;      /* sectors in one copy of the FAT */
	uint32_t root_start;    /* fixed root directory region (FAT12/16) */
	uint32_t root_bytes;
	uint32_t data_start;    /* cluster 2 */
	uint32_t cluster_count; /* valid clusters are 2..cluster_count+1 */
	uint32_t hint;          /* where the next allocation search resumes */
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
	/* Where this node's own directory entry is, so a write can keep it
	 * right: the directory holding it (the fixed root, or a cluster) and
	 * the entry's offset there. has_entry is false for a root itself. */
	bool has_entry;
	bool entry_dir_fixed;
	uint32_t entry_dir_cluster;
	uint32_t entry_off;
};

static const struct vnode_ops fat_ops;
static const struct vnode_ops fat_ro_ops;

static const struct vnode_ops *ops_for(const struct fat_fs *fs)
{
	return fs->read_only ? &fat_ro_ops : &fat_ops;
}

static long node_read(struct fat_node *n, uint32_t offset, uint8_t *buf, uint32_t len);
static int sync_entry(struct fat_node *n);

static uint16_t le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put_le16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
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

/* One sector to the disk. `data` may be the cache itself. Caller holds
 * fs->lock; the cache is left holding this sector, or left alone if it
 * was holding another one (still right: this write didn't touch it). */
static int write_sector(struct fat_fs *fs, uint32_t sector, const uint8_t *data)
{
	if (fs->read_only) {
		return -EROFS;
	}
	if (device_write_blocks(fs->dev, sector, 1, data) != 0) {
		return -EIO;
	}
	if (fs->cache_valid && fs->cached_sector == sector && data != fs->cache) {
		memcpy(fs->cache, data, SECTOR);
	}
	return 0;
}

/* Changes one byte of the file allocation table in every copy of it:
 * (old & mask) | set, read and written copy by copy, so a difference
 * between the copies that nothing is touching stays as it was. */
static int fat_patch_byte(struct fat_fs *fs, uint32_t offset, uint8_t mask, uint8_t set)
{
	for (uint32_t copy = 0; copy < fs->fats; copy++) {
		uint32_t sector = fs->fat_start + copy * fs->fat_size + offset / SECTOR;
		uint32_t within = offset % SECTOR;
		const uint8_t *s = read_sector(fs, sector);
		if (!s) {
			return -EIO;
		}
		uint8_t block[SECTOR];
		memcpy(block, s, SECTOR);
		block[within] = (uint8_t)((block[within] & mask) | set);
		int rc = write_sector(fs, sector, block);
		if (rc) {
			return rc;
		}
	}
	return 0;
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

/* Sets the FAT entry for `cluster` in every copy: the next cluster, an
 * end-of-chain marker, or 0 for a free one. */
static int fat_set_entry(struct fat_fs *fs, uint32_t cluster, uint32_t value)
{
	uint32_t offset = fs->fat12 ? cluster + cluster / 2 : cluster * 2;
	int rc;
	if (!fs->fat12) {
		rc = fat_patch_byte(fs, offset, 0x00, (uint8_t)value);
		return rc ? rc : fat_patch_byte(fs, offset + 1, 0x00, (uint8_t)(value >> 8));
	}
	/* FAT12: two or three bytes, each keeping the nibbles that aren't
	 * this entry's. An odd cluster starts half-way through a byte, and
	 * an entry can end in the next sector. */
	if (cluster & 1) {
		rc = fat_patch_byte(fs, offset, 0x0F, (uint8_t)((value & 0x0F) << 4));
		return rc ? rc : fat_patch_byte(fs, offset + 1, 0x00, (uint8_t)(value >> 4));
	}
	rc = fat_patch_byte(fs, offset, 0xFF, (uint8_t)(value & 0xFF));
	return rc ? rc : fat_patch_byte(fs, offset + 1, 0xF0, (uint8_t)((value >> 8) & 0x0F));
}

static bool is_end_of_chain(struct fat_fs *fs, uint32_t v)
{
	return v >= (fs->fat12 ? 0xFF8u : 0xFFF8u);
}

static bool valid_cluster(struct fat_fs *fs, uint32_t c)
{
	return c >= 2 && c < fs->cluster_count + 2;
}

static uint32_t end_marker(struct fat_fs *fs)
{
	return fs->fat12 ? 0xFFF : 0xFFFF;
}

/* A free cluster, marked end-of-chain so nothing else takes it. */
static int alloc_cluster(struct fat_fs *fs, uint32_t *out)
{
	uint32_t start = fs->hint >= 2 ? fs->hint : 2;
	for (uint32_t i = 0; i < fs->cluster_count; i++) {
		uint32_t c = 2 + ((start - 2 + i) % fs->cluster_count);
		uint32_t v;
		if (fat_entry(fs, c, &v)) {
			return -EIO;
		}
		if (v == 0) {
			int rc = fat_set_entry(fs, c, end_marker(fs));
			if (rc) {
				return rc;
			}
			fs->hint = c + 1 < fs->cluster_count + 2 ? c + 1 : 2;
			*out = c;
			return 0;
		}
	}
	return -ENOSPC;
}

/* Hands every cluster of a chain back, following it at most
 * cluster_count steps so a looping chain can't hang the caller. */
static int free_chain(struct fat_fs *fs, uint32_t first)
{
	fs->hint = valid_cluster(fs, first) ? first : fs->hint;
	for (uint32_t c = first, steps = 0; valid_cluster(fs, c); steps++) {
		uint32_t next;
		if (steps > fs->cluster_count || fat_entry(fs, c, &next)) {
			return -EIO;
		}
		int rc = fat_set_entry(fs, c, 0);
		if (rc) {
			return rc;
		}
		if (is_end_of_chain(fs, next)) {
			break;
		}
		if (!valid_cluster(fs, next)) {
			return -EIO; /* a bad marker in the middle of a chain */
		}
		c = next;
	}
	return 0;
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

/* The index'th cluster of a node's chain, allocating it -- and linking
 * it to the one before -- when a file's chain doesn't reach that far.
 * Directories don't grow: past their last cluster is -ENOSPC. Caller
 * holds the lock. */
static int cluster_for_write(struct fat_node *n, uint32_t index, uint32_t *out)
{
	struct fat_fs *fs = n->fs;
	uint32_t c = n->first_cluster, prev = 0;
	for (uint32_t i = 0; i <= fs->cluster_count; i++) {
		if (!c) { /* nothing here yet: take a free cluster */
			if (n->vnode.type == VNODE_DIR) {
				return -ENOSPC;
			}
			int rc = alloc_cluster(fs, &c);
			if (rc) {
				return rc;
			}
			if (prev) {
				rc = fat_set_entry(fs, prev, c);
				if (rc) {
					return rc;
				}
			} else {
				n->first_cluster = c;
				n->cursor_index = n->cursor_cluster = 0;
				rc = sync_entry(n);
				if (rc) {
					return rc;
				}
			}
		} else if (!valid_cluster(fs, c)) {
			return -EIO; /* a bad cluster in the chain: don't build on it */
		}
		if (i == index) {
			*out = c;
			return 0;
		}
		uint32_t next;
		int rc = fat_entry(fs, c, &next);
		if (rc) {
			return rc;
		}
		prev = c;
		if (is_end_of_chain(fs, next)) {
			c = 0; /* allocate on the next turn */
		} else if (!valid_cluster(fs, next)) {
			return -EIO;
		} else {
			c = next;
		}
	}
	return -EIO; /* a chain as long as the disk: it loops */
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

/* Writes a node's raw contents (file data or directory entries),
 * growing a file's chain as it goes; a fixed root region or a full
 * directory stops it short. Caller holds the lock. */
static long node_write(struct fat_node *n, uint32_t offset, const uint8_t *buf, uint32_t len)
{
	struct fat_fs *fs = n->fs;
	uint32_t done = 0;

	if (n->fixed_root) {
		if (offset >= fs->root_bytes) {
			return -ENOSPC;
		}
		if (len > fs->root_bytes - offset) {
			len = fs->root_bytes - offset; /* a short write: the region is full */
		}
	}
	while (done < len) {
		uint32_t sector;
		if (n->fixed_root) {
			sector = fs->root_start + offset / SECTOR;
		} else {
			uint32_t c;
			int rc = cluster_for_write(n, offset / fs->bytes_per_cluster, &c);
			if (rc) {
				return done ? (long)done : rc;
			}
			sector = fs->data_start + (c - 2) * fs->sectors_per_cluster
			         + (offset % fs->bytes_per_cluster) / SECTOR;
		}
		const uint8_t *s = read_sector(fs, sector);
		if (!s) {
			return done ? (long)done : -EIO;
		}
		uint8_t block[SECTOR];
		memcpy(block, s, SECTOR);
		uint32_t within = offset % SECTOR;
		uint32_t chunk = SECTOR - within < len - done ? SECTOR - within : len - done;
		memcpy(block + within, buf + done, chunk);
		int rc = write_sector(fs, sector, block);
		if (rc) {
			return done ? (long)done : rc;
		}
		done += chunk;
		offset += chunk;
	}
	return (long)done;
}

/* A directory opened by where it is, on the stack: it never goes near
 * the vnode layer, so nothing references it or releases it. */
static void dir_at(struct fat_fs *fs, bool fixed, uint32_t first, struct fat_node *out)
{
	memset(out, 0, sizeof(*out));
	out->fs = fs;
	out->fixed_root = fixed;
	out->first_cluster = first;
	out->vnode.ops = ops_for(fs);
	out->vnode.type = VNODE_DIR;
	out->vnode.refs = 1;
}

/* Puts a node's cluster and size back into its own directory entry, so
 * a listing and a CHKDSK see what a write did. Caller holds the lock. */
static int sync_entry(struct fat_node *n)
{
	if (!n->has_entry) {
		return 0;
	}
	struct fat_node dir;
	uint8_t e[DIRENT_SIZE];
	dir_at(n->fs, n->entry_dir_fixed, n->entry_dir_cluster, &dir);
	if (node_read(&dir, n->entry_off, e, DIRENT_SIZE) != DIRENT_SIZE) {
		return -EIO;
	}
	if (e[0] == ENTRY_DELETED) {
		return -ENOENT; /* deleted under us */
	}
	put_le16(e + 26, n->first_cluster);
	put_le32(e + 28, n->vnode.type == VNODE_DIR ? 0 : n->vnode.size);
	return node_write(&dir, n->entry_off, e, DIRENT_SIZE) == DIRENT_SIZE ? 0 : -EIO;
}

/* Takes the cluster and size the entry on the disk has now: another
 * handle on the same file may have changed them since this node was
 * made. Caller holds the lock. */
static int refresh_entry(struct fat_node *n)
{
	if (!n->has_entry) {
		return 0;
	}
	struct fat_node dir;
	uint8_t e[DIRENT_SIZE];
	dir_at(n->fs, n->entry_dir_fixed, n->entry_dir_cluster, &dir);
	if (node_read(&dir, n->entry_off, e, DIRENT_SIZE) != DIRENT_SIZE) {
		return -EIO;
	}
	if (e[0] == ENTRY_DELETED) {
		return -ENOENT;
	}
	n->first_cluster = le16(e + 26);
	n->vnode.size = le32(e + 28);
	n->cursor_index = n->cursor_cluster = 0;
	return 0;
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

/* Two entries' 11 name bytes, case ignored. */
static bool name11_equal(const uint8_t *a, const uint8_t *b)
{
	for (int i = 0; i < 11; i++) {
		char x = (char)a[i], y = (char)b[i];
		if (x >= 'a' && x <= 'z') {
			x -= 32;
		}
		if (y >= 'a' && y <= 'z') {
			y -= 32;
		}
		if (x != y) {
			return false;
		}
	}
	return true;
}

/* Visits the visible entries of directory `n` (not deleted, long-name,
 * volume-label, "." or ".." entries), with the byte offset each one is
 * at. fn returns true to stop. Caller holds the lock. Returns 0 or -EIO. */
static int for_each_entry(struct fat_node *n,
                          bool (*fn)(const uint8_t *e, const char *name, uint32_t off, void *ctx),
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
		if (fn(e, name, off, ctx)) {
			return 0;
		}
	}
}

static struct fat_node *new_node(struct fat_node *dir, const uint8_t *e, uint32_t off)
{
	struct fat_fs *fs = dir->fs;
	struct fat_node *n = kmalloc(sizeof(*n));
	if (n) {
		memset(n, 0, sizeof(*n));
		n->fs = fs;
		n->first_cluster = le16(e + 26);
		n->vnode.ops = ops_for(fs);
		n->vnode.type = (e[11] & ATTR_DIRECTORY) ? VNODE_DIR : VNODE_FILE;
		n->vnode.size = (e[11] & ATTR_DIRECTORY) ? 0 : le32(e + 28);
		n->vnode.refs = 1;
		n->has_entry = true;
		n->entry_dir_fixed = dir->fixed_root;
		n->entry_dir_cluster = dir->first_cluster;
		n->entry_off = off;
	}
	return n;
}

/* The entry a name of ours lives in (below, under naming). */
static int name11_of(const char *name, uint8_t out[11]);

struct walk_ctx {
	const char *want;
	uint8_t alias[11]; /* what `want` is called in an entry, if it has
	                  * one name; a name that doesn't fit 8.3 is
	                  * stored under its "~1" alias */
	bool has_alias;
	uint8_t found[DIRENT_SIZE];
	uint32_t off;
	bool hit;
};

static void want_ctx(struct walk_ctx *w, const char *name)
{
	memset(w, 0, sizeof(*w));
	w->want = name;
	w->has_alias = name11_of(name, w->alias) == 0;
}

/* The name as the directory holds it: its short name, or the alias a
 * name that doesn't fit was given when it was created -- there is no
 * long-name entry beside it to hold the rest of the name (see naming). */
static bool match_name(const uint8_t *e, const char *name, uint32_t off, void *ctx)
{
	struct walk_ctx *w = ctx;
	if (names_equal_nocase(name, w->want) || (w->has_alias && name11_equal(e, w->alias))) {
		memcpy(w->found, e, DIRENT_SIZE);
		w->off = off;
		w->hit = true;
		return true;
	}
	return false;
}

static int fat_walk(struct vnode *dir, const char *name, struct vnode **out)
{
	struct fat_node *d = (struct fat_node *)dir;
	struct walk_ctx w;
	want_ctx(&w, name);

	mutex_lock(&d->fs->lock);
	int rc = for_each_entry(d, match_name, &w);
	mutex_unlock(&d->fs->lock);
	if (rc) {
		return rc;
	}
	if (!w.hit) {
		return -ENOENT;
	}
	struct fat_node *n = new_node(d, w.found, w.off);
	if (!n) {
		return -ENOMEM;
	}
	*out = &n->vnode;
	return 0;
}

/* The entry named `name` (as walk finds it), its offset, and itself. */
static int entry_find(struct fat_node *dir, const char *name, uint32_t *off, uint8_t *e)
{
	struct walk_ctx w;
	want_ctx(&w, name);
	int rc = for_each_entry(dir, match_name, &w);
	if (rc) {
		return rc;
	}
	if (!w.hit) {
		return -ENOENT;
	}
	*off = w.off;
	memcpy(e, w.found, DIRENT_SIZE);
	return 0;
}

struct taken_ctx {
	const uint8_t *want;
	uint32_t skip;    /* an entry to look past: the one being renamed */
	bool skipping;
	bool taken;
};

static bool match11(const uint8_t *e, const char *name, uint32_t off, void *ctx)
{
	struct taken_ctx *t = ctx;
	(void)name;
	if (t->skipping && off == t->skip) {
		return false;
	}
	if (name11_equal(e, t->want)) {
		t->taken = true;
		return true;
	}
	return false;
}

/* Whether the directory holds exactly these 11 name bytes. */
static int name_taken(struct fat_node *dir, const uint8_t want[11], uint32_t skip, bool skipping)
{
	struct taken_ctx t = { .want = want, .skip = skip, .skipping = skipping, .taken = false };
	int rc = for_each_entry(dir, match11, &t);
	return rc ? rc : t.taken;
}

/* --- naming:8.3, with the numeric tail the FAT specification gives a
 * name that doesn't fit --- */

static char upper_char(char c)
{
	return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

/* A character a directory entry may hold besides a letter or a digit.
 * Anything else becomes '_', which is what a FAT name can say. */
static char name_char(char c)
{
	static const char ALLOWED[] = "$%'-_@~!#&()^`{}";
	c = upper_char(c);
	if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
		return c;
	}
	for (const char *p = ALLOWED; *p; p++) {
		if (*p == c) {
			return c;
		}
	}
	return '_';
}

static void pack11(const char *base, size_t blen, const char *ext, size_t elen, uint8_t out[11])
{
	for (int i = 0; i < 8; i++) {
		out[i] = (uint8_t)(i < (int)blen ? base[i] : ' ');
	}
	for (int i = 0; i < 3; i++) {
		out[8 + i] = (uint8_t)(i < (int)elen ? ext[i] : ' ');
	}
}

/* A name split into the base and extension a directory entry holds,
 * with every character already one a FAT name may say. */
struct fat_name {
	char base[VFS_NAME_MAX + 1];
	size_t blen;
	char ext[4];
	size_t elen;
};

static int split_name(const char *name, struct fat_name *f)
{
	size_t len = strlen(name);
	const char *dot = 0;

	for (size_t i = 0; i < len; i++) {
		if (name[i] == '.') {
			dot = name + i;
		}
	}
	size_t base_len = dot && dot > name ? (size_t)(dot - name) : len;
	f->blen = 0;
	f->elen = 0;
	if (dot && dot > name) {
		for (const char *p = dot + 1; *p && f->elen < 3; p++) {
			f->ext[f->elen++] = name_char(*p);
		}
	}
	if (base_len == 0) { /* ".profile": no base, so the extension's part */
		base_len = len;
		f->elen = 0;
	}
	for (size_t i = 0; i < base_len; i++) {
		f->base[f->blen++] = name_char(name[i]);
	}
	return f->blen ? 0 : -EINVAL;
}

/* The 11 bytes of alias `n` (N 1..999) of a base that doesn't fit: the
 * spec's "~N" carries it, taking as much of the base as leaves room. */
static void alias11(const struct fat_name *f, unsigned n, uint8_t out[11])
{
	int digits = n < 10 ? 1 : n < 100 ? 2 : 3;
	int keep = 8 - 1 - digits;
	if (keep < 1) {
		keep = 1;
	}
	if (keep > (int)f->blen) {
		keep = (int)f->blen;
	}
	char cand[9];
	memcpy(cand, f->base, (size_t)keep);
	cand[keep] = '~';
	int at = keep + 1;
	if (n >= 100) {
		cand[at++] = (char)('0' + n / 100);
	}
	if (n >= 10) {
		cand[at++] = (char)('0' + n / 10 % 10);
	}
	cand[at++] = (char)('0' + n % 10);
	pack11(cand, (size_t)at, f->ext, f->elen, out);
}

/* The one entry a name of ours can live in: itself when it fits 8.3,
 * and otherwise the first alias make_entry_name gives it. ManiOS writes
 * no long-name entries, so a name that doesn't fit has its alias for a
 * name -- this is how a file we created under one is found again. */
static int name11_of(const char *name, uint8_t out[11])
{
	struct fat_name f;
	int rc = split_name(name, &f);
	if (rc) {
		return rc;
	}
	if (f.blen <= 8 && f.elen <= 3) {
		pack11(f.base, f.blen, f.ext, f.elen, out);
		return 0;
	}
	alias11(&f, 1, out);
	return 0;
}

/* The 11 bytes of a directory entry for `name`: uppercase 8.3 when it
 * fits, and otherwise the base shortened to carry "~N" (N 1..999), as
 * the FAT specification describes. The result mustn't be an entry the
 * directory already has -- `skip`'s entry is passed over, for renaming
 * a name to itself. */
static int make_entry_name(struct fat_node *dir, const char *name, uint32_t skip, bool skipping,
                           uint8_t out[11])
{
	struct fat_name f;
	int rc = split_name(name, &f);
	if (rc) {
		return rc;
	}
	int taken;
	if (f.blen <= 8 && f.elen <= 3) {
		pack11(f.base, f.blen, f.ext, f.elen, out);
		taken = name_taken(dir, out, skip, skipping);
		if (taken <= 0) {
			return taken; /* 0: it's free */
		}
	}
	for (unsigned n = 1; n <= 999; n++) {
		alias11(&f, n, out);
		taken = name_taken(dir, out, skip, skipping);
		if (taken <= 0) {
			return taken;
		}
	}
	return -ENOSPC; /* every alias of it is taken */
}

/* A deleted entry to reuse, or the directory's end marker (which
 * entry_place moves on one slot). *at_end says which. */
static int entry_slot(struct fat_node *dir, uint32_t *off, bool *at_end)
{
	uint8_t e[DIRENT_SIZE];
	for (uint32_t at = 0;; at += DIRENT_SIZE) {
		long got = node_read(dir, at, e, DIRENT_SIZE);
		if (got < 0) {
			return -EIO;
		}
		if (got < DIRENT_SIZE) {
			break; /* the directory is full */
		}
		if (e[0] == ENTRY_DELETED) {
			*off = at;
			*at_end = false;
			return 0;
		}
		if (e[0] == ENTRY_END) {
			*off = at;
			*at_end = true;
			return 0;
		}
	}
	return -ENOSPC;
}

/* Puts an entry in its slot, and after an end marker taken for it,
 * writes a new one where there's room for it. */
static int entry_place(struct fat_node *dir, uint32_t off, const uint8_t *e, bool at_end)
{
	if (node_write(dir, off, e, DIRENT_SIZE) != DIRENT_SIZE) {
		return -ENOSPC;
	}
	if (!at_end) {
		return 0;
	}
	uint8_t zero[DIRENT_SIZE];
	long next = node_read(dir, off + DIRENT_SIZE, zero, DIRENT_SIZE);
	if (next < 0) {
		return -EIO;
	}
	if (next == DIRENT_SIZE) {
		memset(zero, 0, DIRENT_SIZE);
		if (node_write(dir, off + DIRENT_SIZE, zero, DIRENT_SIZE) != DIRENT_SIZE) {
			return -ENOSPC;
		}
	}
	return 0;
}

/* Where an entry's long-name entries start: back from it while they
 * go on. Only they, so a name of ours never loses anything. */
static int lfn_start(struct fat_node *dir, uint32_t off, uint32_t *start)
{
	*start = off;
	for (uint32_t at = off; at >= DIRENT_SIZE;) {
		uint8_t e[DIRENT_SIZE];
		at -= DIRENT_SIZE;
		if (node_read(dir, at, e, DIRENT_SIZE) != DIRENT_SIZE) {
			return -EIO;
		}
		if (e[11] != ATTR_LONG_NAME) {
			break;
		}
		*start = at;
	}
	return 0;
}

/* Marks the slots from `start` to `off` deleted, so no long name is
 * left pointing at an entry that has changed or gone. */
static int slots_delete(struct fat_node *dir, uint32_t start, uint32_t off)
{
	for (uint32_t at = start; at <= off; at += DIRENT_SIZE) {
		uint8_t e[DIRENT_SIZE];
		if (node_read(dir, at, e, DIRENT_SIZE) != DIRENT_SIZE) {
			return -EIO;
		}
		e[0] = ENTRY_DELETED;
		if (node_write(dir, at, e, DIRENT_SIZE) != DIRENT_SIZE) {
			return -EIO;
		}
	}
	return 0;
}

static int entry_delete(struct fat_node *dir, uint32_t off)
{
	uint32_t start;
	int rc = lfn_start(dir, off, &start);
	return rc ? rc : slots_delete(dir, start, off);
}

/* --- the operations --- */

/* "." (in a directory: the directory's own cluster) and ".." (its
 * parent's, or 0 when that is the fixed root), with an end marker, all
 * written at once so a reused cluster holds no old entries behind them. */
static int new_directory(struct fat_node *parent, uint32_t cluster)
{
	struct fat_fs *fs = parent->fs;
	struct fat_node dir;
	uint8_t block[3 * DIRENT_SIZE];

	memset(block, 0, sizeof(block));
	for (int i = 0; i < 2; i++) {
		uint8_t *e = block + i * DIRENT_SIZE;
		memset(e, ' ', 11); /* "." and "..", padded as 8.3 pads them */
		e[0] = '.';
		if (i) {
			e[1] = '.';
		}
		e[11] = ATTR_DIRECTORY;
		put_le16(e + 16, FAT_DATE_EPOCH);
		put_le16(e + 24, FAT_DATE_EPOCH);
	}
	put_le16(block + 26, cluster);                    /* "." is this directory */
	put_le16(block + DIRENT_SIZE + 26, parent->fixed_root ? 0 : parent->first_cluster);

	dir_at(fs, false, cluster, &dir);
	return node_write(&dir, 0, block, sizeof(block)) == (long)sizeof(block) ? 0 : -EIO;
}

static int fat_create(struct vnode *vdir, const char *name, enum vnode_type type, struct vnode **out)
{
	struct fat_node *d = (struct fat_node *)vdir;
	struct fat_fs *fs = d->fs;
	uint8_t e[DIRENT_SIZE], name11[11];
	uint32_t off = 0, first = 0;
	bool at_end = false;
	int rc = 0;

	*out = 0;
	if (fs->read_only) {
		return -EROFS;
	}
	mutex_lock(&fs->lock);
	rc = entry_find(d, name, &off, e);
	if (rc == 0) {
		rc = -EEXIST;
	}
	if (rc && rc != -ENOENT) {
		goto out;
	}
	rc = make_entry_name(d, name, 0, false, name11);
	if (rc) {
		goto out;
	}
	rc = entry_slot(d, &off, &at_end);
	if (rc) {
		goto out;
	}
	memset(e, 0, sizeof(e));
	memcpy(e, name11, 11);
	e[11] = type == VNODE_DIR ? ATTR_DIRECTORY : ATTR_ARCHIVE;
	put_le16(e + 16, FAT_DATE_EPOCH);
	put_le16(e + 24, FAT_DATE_EPOCH);
	if (type == VNODE_DIR) {
		rc = alloc_cluster(fs, &first);
		if (rc) {
			goto out;
		}
		rc = new_directory(d, first);
		if (rc) {
			free_chain(fs, first);
			goto out;
		}
		put_le16(e + 26, first);
	}
	rc = entry_place(d, off, e, at_end);
	if (rc) {
		if (type == VNODE_DIR) {
			free_chain(fs, first);
		}
		goto out;
	}
	struct fat_node *n = kmalloc(sizeof(*n));
	if (!n) {
		entry_delete(d, off);
		if (type == VNODE_DIR) {
			free_chain(fs, first);
		}
		rc = -ENOMEM;
		goto out;
	}
	memset(n, 0, sizeof(*n));
	n->fs = fs;
	n->first_cluster = first;
	n->vnode.ops = ops_for(fs);
	n->vnode.type = type;
	n->vnode.refs = 1;
	n->has_entry = true;
	n->entry_dir_fixed = d->fixed_root;
	n->entry_dir_cluster = d->first_cluster;
	n->entry_off = off;
	*out = &n->vnode;
	rc = 0;
out:
	mutex_unlock(&fs->lock);
	return rc;
}

/* A directory whose entries are only "." and ".." (and, on a disk that
 * was written elsewhere, whatever they were followed by: nothing that
 * a file or a directory of ours is called). */
static int dir_is_empty(struct fat_node *dir)
{
	uint8_t e[DIRENT_SIZE];
	for (uint32_t off = 0;; off += DIRENT_SIZE) {
		long got = node_read(dir, off, e, DIRENT_SIZE);
		if (got < 0) {
			return -EIO;
		}
		if (got < DIRENT_SIZE || e[0] == ENTRY_END) {
			return 0;
		}
		if (e[0] == ENTRY_DELETED || e[11] == ATTR_LONG_NAME || (e[11] & ATTR_VOLUME_ID)) {
			continue;
		}
		bool dot = e[0] == '.' && (e[1] == ' ' || (e[1] == '.' && e[2] == ' '));
		if (!dot) {
			return -ENOTEMPTY;
		}
	}
}

static int fat_remove(struct vnode *vdir, const char *name, enum vnode_type expect)
{
	struct fat_node *d = (struct fat_node *)vdir;
	struct fat_fs *fs = d->fs;
	uint8_t e[DIRENT_SIZE];
	uint32_t off = 0;
	int rc;

	if (fs->read_only) {
		return -EROFS;
	}
	mutex_lock(&fs->lock);
	if ((rc = entry_find(d, name, &off, e)) != 0) {
		goto out;
	}
	bool is_dir = (e[11] & ATTR_DIRECTORY) != 0;
	if (expect == VNODE_FILE && is_dir) {
		rc = -EISDIR;
		goto out;
	}
	if (expect == VNODE_DIR && !is_dir) {
		rc = -ENOTDIR;
		goto out;
	}
	if (is_dir) {
		struct fat_node sub;
		dir_at(fs, false, le16(e + 26), &sub);
		if ((rc = dir_is_empty(&sub)) != 0) {
			goto out;
		}
	}
	if ((rc = free_chain(fs, le16(e + 26))) != 0) {
		goto out;
	}
	rc = entry_delete(d, off);
out:
	mutex_unlock(&fs->lock);
	return rc;
}

/* What stands in the way of a rename: the target's long-name slots (on
 * a disk written elsewhere), its entry, and the chains of the names it
 * holds, go. Only a directory whose name is also a directory's, and
 * only an empty one (POSIX). */
static int drop_target(struct fat_node *dir, const uint8_t *src, const uint8_t *tgt,
                       uint32_t toff)
{
	bool src_dir = (src[11] & ATTR_DIRECTORY) != 0;
	bool tgt_dir = (tgt[11] & ATTR_DIRECTORY) != 0;
	uint32_t lfn = 0;
	int rc;

	if (src_dir != tgt_dir) {
		return src_dir ? -ENOTDIR : -EISDIR;
	}
	if (tgt_dir) {
		struct fat_node sub;
		dir_at(dir->fs, false, le16(tgt + 26), &sub);
		if ((rc = dir_is_empty(&sub)) != 0) {
			return rc;
		}
	}
	if ((rc = lfn_start(dir, toff, &lfn)) != 0) {
		return rc;
	}
	if (lfn < toff && (rc = slots_delete(dir, lfn, toff - DIRENT_SIZE)) != 0) {
		return rc;
	}
	if ((rc = free_chain(dir->fs, le16(tgt + 26))) != 0) {
		return rc;
	}
	return entry_delete(dir, toff);
}

static int fat_rename(struct vnode *vdir, const char *from, const char *to)
{
	struct fat_node *d = (struct fat_node *)vdir;
	struct fat_fs *fs = d->fs;
	uint8_t e[DIRENT_SIZE], other[DIRENT_SIZE], name11[11];
	uint32_t off = 0, ooff = 0, lfn = 0;
	int rc;

	if (fs->read_only) {
		return -EROFS;
	}
	mutex_lock(&fs->lock);
	if ((rc = entry_find(d, from, &off, e)) != 0) {
		goto out;
	}
	rc = entry_find(d, to, &ooff, other);
	if (rc == 0 && ooff != off) {
		/* rename() puts the new name over what the old one had: only
		 * an empty directory goes without a fight (drop_target). */
		rc = drop_target(d, e, other, ooff);
		if (rc) {
			goto out;
		}
	} else if (rc && rc != -ENOENT) {
		/* rc == 0 && ooff == off is the file under its own name: only
		 * the bytes below can change. */
		goto out;
	}
	/* The old name is passed over: renaming to itself, or to the same
	 * name in another case, changes nothing but the bytes. */
	if ((rc = make_entry_name(d, to, off, true, name11)) != 0) {
		goto out;
	}
	if ((rc = lfn_start(d, off, &lfn)) != 0) {
		goto out;
	}
	if (lfn < off && (rc = slots_delete(d, lfn, off - DIRENT_SIZE)) != 0) {
		goto out;
	}
	memcpy(e, name11, 11);
	rc = entry_place(d, off, e, false);
out:
	mutex_unlock(&fs->lock);
	return rc;
}

static int fat_truncate(struct vnode *v, uint32_t size)
{
	struct fat_node *n = (struct fat_node *)v;
	struct fat_fs *fs = n->fs;
	int rc = 0;

	if (fs->read_only) {
		return -EROFS;
	}
	mutex_lock(&fs->lock);
	if ((rc = refresh_entry(n)) != 0) {
		goto out;
	}
	if (size == 0) {
		rc = free_chain(fs, n->first_cluster);
		n->first_cluster = 0;
		n->cursor_index = n->cursor_cluster = 0;
		v->size = 0;
		if (!rc) {
			rc = sync_entry(n);
		}
	} else if (size > v->size) {
		rc = -EINVAL; /* a file grows by writing, not by being emptied */
	} else if (size < v->size) {
		/* Keep the clusters the new size needs; hand back the rest. */
		uint32_t keep = (size + fs->bytes_per_cluster - 1) / fs->bytes_per_cluster;
		uint32_t c = n->first_cluster;
		for (uint32_t i = 0; c && valid_cluster(fs, c) && i + 1 < keep; i++) {
			uint32_t next;
			if (fat_entry(fs, c, &next)) {
				rc = -EIO;
				goto out;
			}
			if (is_end_of_chain(fs, next)) {
				break;
			}
			c = next;
		}
		if (!valid_cluster(fs, c)) {
			rc = -EIO; /* no chain to shorten */
			goto out;
		}
		uint32_t next;
		if (!fat_entry(fs, c, &next)) {
			if (!is_end_of_chain(fs, next)) {
				rc = free_chain(fs, next);
			}
			if (!rc) {
				rc = fat_set_entry(fs, c, end_marker(fs));
			}
		} else {
			rc = -EIO;
		}
		if (!rc) {
			v->size = size;
			rc = sync_entry(n);
		}
	}
out:
	mutex_unlock(&fs->lock);
	return rc;
}

static long fat_write(struct vnode *v, uint32_t offset, const void *buf, size_t len)
{
	struct fat_node *n = (struct fat_node *)v;
	struct fat_fs *fs = n->fs;
	long done = 0;
	int rc = 0;

	if (fs->read_only) {
		return -EROFS;
	}
	if (v->type == VNODE_DIR) {
		return -EISDIR;
	}
	mutex_lock(&fs->lock);
	if ((rc = refresh_entry(n)) == 0) {
		done = node_write(n, offset, buf, (uint32_t)len);
		if (done > 0 && offset + (uint32_t)done > v->size) {
			v->size = offset + (uint32_t)done;
			rc = sync_entry(n);
		}
	}
	mutex_unlock(&fs->lock);
	if (done > 0) {
		return done;
	}
	return rc ? rc : done;
}

struct readdir_ctx {
	uint32_t skip;
	struct dirent *out;
	bool hit;
};

static bool nth_entry(const uint8_t *e, const char *name, uint32_t off, void *ctx)
{
	struct readdir_ctx *r = ctx;
	(void)off;
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
	.write = fat_write,
	.readdir = fat_readdir,
	.create = fat_create,
	.remove = fat_remove,
	.rename = fat_rename,
	.truncate = fat_truncate,
	.release = fat_release,
};

/* A volume on a disk that takes no writes: everything but writing. */
static const struct vnode_ops fat_ro_ops = {
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
	fs->read_only = !dev->block_ops->write;
	fs->fats = fats;
	fs->sectors_per_cluster = per_cluster;
	fs->bytes_per_cluster = per_cluster * SECTOR;
	fs->fat_start = reserved;
	fs->fat_size = fat_size;
	fs->root_start = reserved + fats * fat_size;
	fs->root_bytes = root_entries * DIRENT_SIZE;
	fs->data_start = data_start;
	fs->cluster_count = clusters;
	fs->hint = 2;

	struct fat_node *r = kmalloc(sizeof(*r));
	if (!r) {
		kfree(fs);
		return -ENOMEM;
	}
	memset(r, 0, sizeof(*r));
	r->fs = fs;
	r->fixed_root = true;
	r->vnode.ops = ops_for(fs);
	r->vnode.type = VNODE_DIR;
	r->vnode.refs = 1;
	*root = &r->vnode;
	if (total < 4 * 2048) { /* floppies: 1440 KiB, not "1 MiB" */
		ksnprintf(desc, desc_len, "%s, %lu KiB%s", fs->fat12 ? "FAT12" : "FAT16", total / 2,
		          fs->read_only ? ", read-only" : "");
	} else {
		ksnprintf(desc, desc_len, "%s, %lu MiB%s", fs->fat12 ? "FAT12" : "FAT16", total / 2048,
		          fs->read_only ? ", read-only" : "");
	}
	return 0;
}
