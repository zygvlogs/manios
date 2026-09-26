/* FAT volumes read from the disk itself (dos.h): what VOL, DIR and
 * CHKDSK need that ManiOS's file system doesn't say -- the label, the
 * serial number, free space -- and CHKDSK's check of every cluster
 * chain against the FAT. Read only: ManiDOS reports problems, it
 * doesn't correct them. FAT12 and FAT16, as ManiOS mounts. */
#include "dos.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ATTR_VOLUME 0x08
#define ATTR_DIR    0x10
#define ATTR_LFN    0x0F
#define MAX_DEPTH   16

static uint32_t le16(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t le32(const uint8_t *p)
{
	return le16(p) | le16(p + 2) << 16;
}

static int read_at(int fd, uint32_t offset, void *buf, uint32_t len)
{
	if (lseek(fd, (long)offset, SEEK_SET) < 0) {
		return -1;
	}
	uint32_t got = 0;
	while (got < len) {
		long n = read(fd, (uint8_t *)buf + got, len - got);
		if (n <= 0) {
			return -1;
		}
		got += (uint32_t)n;
	}
	return 0;
}

static uint32_t fat_entry(const uint8_t *fat, int bits, uint32_t n)
{
	if (bits == 16) {
		return le16(fat + 2 * n);
	}
	uint32_t v = le16(fat + n + n / 2);
	return n & 1 ? v >> 4 : v & 0xFFF;
}

static bool is_bad(uint32_t v, int bits)
{
	return v == (bits == 16 ? 0xFFF7u : 0xFF7u);
}

static bool is_end(uint32_t v, int bits)
{
	return v >= (bits == 16 ? 0xFFF8u : 0xFF8u);
}

static void trim_name(const uint8_t *e, char *out)
{
	int n = 0;
	for (int i = 0; i < 8 && e[i] != ' '; i++) {
		out[n++] = (char)(i == 0 && e[0] == 0x05 ? 0xE5 : e[i]);
	}
	if (e[8] != ' ') {
		out[n++] = '.';
		for (int i = 8; i < 11 && e[i] != ' '; i++) {
			out[n++] = (char)e[i];
		}
	}
	out[n] = '\0';
}

/* Reads the boot sector (and, with `fat`, the first FAT). */
static int open_volume(int fd, struct fat_volume *v, uint8_t **fat, uint8_t **fat2)
{
	uint8_t b[512];
	memset(v, 0, sizeof(*v));
	if (read_at(fd, 0, b, sizeof(b)) < 0 || b[510] != 0x55 || b[511] != 0xAA) {
		return -1;
	}
	v->sector_size = le16(b + 11);
	uint32_t spc = b[13];
	v->reserved = le16(b + 14);
	v->fats = b[16];
	v->root_entries = le16(b + 17);
	uint32_t total = le16(b + 19) ? le16(b + 19) : le32(b + 32);
	v->fat_sectors = le16(b + 22);
	if ((v->sector_size != 512 && v->sector_size != 1024 && v->sector_size != 2048
	     && v->sector_size != 4096) || !spc || (spc & (spc - 1)) || !v->fats || !v->fat_sectors
	    || !v->reserved) {
		return -1; /* not FAT12/16 (FAT32 has no 16-bit FAT size) */
	}
	v->cluster_size = spc * v->sector_size;
	v->root_start = v->reserved + v->fats * v->fat_sectors;
	v->data_start = v->root_start + (v->root_entries * 32 + v->sector_size - 1) / v->sector_size;
	if (total <= v->data_start) {
		return -1;
	}
	v->clusters = (total - v->data_start) / spc;
	v->bits = v->clusters < 4085 ? 12 : v->clusters < 65525 ? 16 : 0;
	if (!v->bits) {
		return -1;
	}
	if (b[38] == 0x29) {
		v->has_serial = true;
		v->serial = le32(b + 39);
		memcpy(v->label, b + 43, 11);
		v->label[11] = '\0';
	}
	/* The root directory's label entry wins over the boot sector's. */
	uint32_t root_bytes = v->root_entries * 32;
	uint8_t *root = malloc(root_bytes);
	if (root && read_at(fd, v->root_start * v->sector_size, root, root_bytes) == 0) {
		for (uint32_t i = 0; i < v->root_entries; i++) {
			uint8_t *e = root + 32 * i;
			if (!e[0]) {
				break;
			}
			if (e[0] != 0xE5 && e[11] != ATTR_LFN && (e[11] & ATTR_VOLUME)) {
				memcpy(v->label, e, 11);
				v->label[11] = '\0';
				break;
			}
		}
	}
	free(root);
	for (int i = 10; i >= 0 && (v->label[i] == ' ' || !v->label[i]); i--) {
		v->label[i] = '\0';
	}
	if (!strcmp(v->label, "NO NAME")) {
		v->label[0] = '\0'; /* what formatting leaves: no label */
	}

	uint32_t fat_bytes = v->fat_sectors * v->sector_size;
	uint8_t *f = malloc(fat_bytes);
	if (!f || read_at(fd, v->reserved * v->sector_size, f, fat_bytes) < 0) {
		free(f);
		return -1;
	}
	for (uint32_t c = 2; c < v->clusters + 2; c++) {
		uint32_t e = fat_entry(f, v->bits, c);
		v->free_clusters += e == 0;
		v->bad_clusters += is_bad(e, v->bits);
	}
	if (fat2) {
		*fat2 = NULL;
		if (v->fats > 1) {
			*fat2 = malloc(fat_bytes);
			if (*fat2 && read_at(fd, (v->reserved + v->fat_sectors) * v->sector_size, *fat2,
			                     fat_bytes) < 0) {
				free(*fat2);
				*fat2 = NULL;
			}
		}
	}
	if (fat) {
		*fat = f;
	} else {
		free(f);
	}
	return 0;
}

int fat_open(const char *device, struct fat_volume *v)
{
	int fd = open(device, OREAD);
	if (fd < 0) {
		return -1;
	}
	int rc = open_volume(fd, v, NULL, NULL);
	close(fd);
	return rc;
}

/* --- CHKDSK --- */

struct check {
	int fd;
	struct fat_volume v;
	uint8_t *fat;
	uint8_t *seen;      /* per cluster: 0, or who holds it */
	char letter;
	bool verbose;
	int problems;
	uint32_t files, dirs, file_clusters, dir_clusters;
};

static void problem(struct check *k, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void problem(struct check *k, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	k->problems++;
}

/* Follows a chain, claiming its clusters: how many it has. */
static uint32_t follow(struct check *k, uint32_t first, const char *path, bool dir)
{
	uint32_t n = 0, c = first;
	while (c >= 2 && !is_end(c, k->v.bits)) {
		if (c >= k->v.clusters + 2) {
			problem(k, "%s  Has an invalid allocation unit (%lu)", path, (unsigned long)c);
			break;
		}
		if (k->seen[c]) {
			problem(k, "%s  Is cross-linked on allocation unit %lu", path, (unsigned long)c);
			break;
		}
		k->seen[c] = dir ? 2 : 1;
		n++;
		uint32_t next = fat_entry(k->fat, k->v.bits, c);
		if (next == 0 || is_bad(next, k->v.bits)) {
			problem(k, "%s  Has a chain that runs into a %s allocation unit after %lu", path,
			        next ? "bad" : "free", (unsigned long)c);
			break;
		}
		c = next;
	}
	return n;
}

static bool walk(struct check *k, const uint8_t *entries, uint32_t count, const char *dir_path,
                 int depth);

static void check_entry(struct check *k, const uint8_t *e, const char *dir_path, int depth)
{
	char name[13], path[PATH_MAX_DOS + 16];
	trim_name(e, name);
	snprintf(path, sizeof(path), "%s\\%s", dir_path, name);
	uint32_t first = le16(e + 26), size = le32(e + 28);
	if (e[11] & ATTR_DIR) {
		k->dirs++;
		if (k->verbose) {
			printf("%s\n", path);
		}
		uint32_t n = follow(k, first, path, true);
		k->dir_clusters += n;
		if (depth >= MAX_DEPTH || !n) {
			return;
		}
		/* Its entries, a cluster at a time along its chain (each read
		 * into a buffer of its own: the walk goes deeper meanwhile). */
		uint8_t *entries = malloc(k->v.cluster_size);
		if (!entries) {
			return;
		}
		for (uint32_t c = first, i = 0; i < n; i++) {
			uint32_t sector = k->v.data_start + (c - 2) * (k->v.cluster_size / k->v.sector_size);
			if (read_at(k->fd, sector * k->v.sector_size, entries, k->v.cluster_size) < 0) {
				problem(k, "%s  Can't be read", path);
				break;
			}
			if (walk(k, entries, k->v.cluster_size / 32, path, depth + 1)) {
				break; /* its end marker */
			}
			c = fat_entry(k->fat, k->v.bits, c);
		}
		free(entries);
		return;
	}
	k->files++;
	if (k->verbose) {
		printf("%s\n", path);
	}
	uint32_t n = follow(k, first, path, false);
	k->file_clusters += n;
	uint32_t need = (size + k->v.cluster_size - 1) / k->v.cluster_size;
	if (n != need) {
		problem(k, "%s  Allocation error: its size needs %lu allocation units, its chain has %lu",
		        path, (unsigned long)need, (unsigned long)n);
	}
}

/* True at the directory's end marker. */
static bool walk(struct check *k, const uint8_t *entries, uint32_t count, const char *dir_path,
                 int depth)
{
	for (uint32_t i = 0; i < count; i++) {
		const uint8_t *e = entries + 32 * i;
		if (!e[0]) {
			return true;
		}
		if (e[0] == 0xE5 || e[11] == ATTR_LFN || (e[11] & ATTR_VOLUME) || e[0] == '.') {
			continue;
		}
		check_entry(k, e, dir_path, depth);
	}
	return false;
}

int fat_check(const char *device, char letter, bool verbose)
{
	struct check k = { .letter = letter, .verbose = verbose };
	uint8_t *fat2 = NULL;
	k.fd = open(device, OREAD);
	if (k.fd < 0 || open_volume(k.fd, &k.v, &k.fat, &fat2) < 0) {
		if (k.fd >= 0) {
			close(k.fd);
		}
		return -1;
	}
	k.seen = calloc(k.v.clusters + 2, 1);
	uint32_t root_bytes = k.v.root_entries * 32;
	uint8_t *root = malloc(root_bytes);
	if (!k.seen || !root
	    || read_at(k.fd, k.v.root_start * k.v.sector_size, root, root_bytes) < 0) {
		printf("Not enough memory, or the root directory can't be read\n");
		close(k.fd);
		return -1;
	}
	if (k.v.label[0]) {
		printf("Volume %s\n", k.v.label);
	}
	if (k.v.has_serial) {
		printf("Volume Serial Number is %04lX-%04lX\n", (unsigned long)(k.v.serial >> 16),
		       (unsigned long)(k.v.serial & 0xFFFF));
	}

	char top[4] = { letter, ':', '\0' };
	walk(&k, root, k.v.root_entries, top, 0);

	/* Clusters the FAT says are used, that no file or directory has
	 * (LOST); a chain of them starts at one no other points to. */
	enum { LOST = 3, LOST_POINTED = 4 };
	uint32_t lost = 0, chains = 0;
	for (uint32_t c = 2; c < k.v.clusters + 2; c++) {
		uint32_t e = fat_entry(k.fat, k.v.bits, c);
		if (e && !is_bad(e, k.v.bits) && !k.seen[c]) {
			lost++;
			k.seen[c] = LOST;
		}
	}
	for (uint32_t c = 2; c < k.v.clusters + 2; c++) {
		uint32_t next = fat_entry(k.fat, k.v.bits, c);
		if ((k.seen[c] == LOST || k.seen[c] == LOST_POINTED) && next >= 2
		    && next < k.v.clusters + 2 && k.seen[next] == LOST) {
			k.seen[next] = LOST_POINTED;
		}
	}
	for (uint32_t c = 2; c < k.v.clusters + 2; c++) {
		chains += k.seen[c] == LOST;
	}
	if (lost) {
		printf("%lu lost allocation units found in %lu chains.\n", (unsigned long)lost,
		       (unsigned long)chains);
		k.problems++;
	}
	if (fat2 && memcmp(k.fat, fat2, k.v.fat_sectors * k.v.sector_size)) {
		printf("The copies of the file allocation table differ.\n");
		k.problems++;
	}
	if (k.problems) {
		printf("\n%d problem%s found. ManiDOS doesn't correct them: ManiOS's drives are read-only.\n",
		       k.problems, k.problems == 1 ? "" : "s");
	}

	unsigned long long cs = k.v.cluster_size;
	putchar('\n');
	print_count(cs * k.v.clusters, 13);
	printf(" bytes total disk space\n");
	if (k.v.bad_clusters) {
		print_count(cs * k.v.bad_clusters, 13);
		printf(" bytes in bad sectors\n");
	}
	print_count(cs * k.dir_clusters, 13);
	printf(" bytes in %lu director%s\n", (unsigned long)k.dirs, k.dirs == 1 ? "y" : "ies");
	print_count(cs * k.file_clusters, 13);
	printf(" bytes in %lu user file%s\n", (unsigned long)k.files, k.files == 1 ? "" : "s");
	if (lost) {
		print_count(cs * lost, 13);
		printf(" bytes in lost chains\n");
	}
	print_count(cs * k.v.free_clusters, 13);
	printf(" bytes available on disk\n\n");
	print_count(cs, 13);
	printf(" bytes in each allocation unit\n");
	print_count(k.v.clusters, 13);
	printf(" total allocation units on disk\n");
	print_count(k.v.free_clusters, 13);
	printf(" available allocation units on disk\n");

	free(root);
	free(k.seen);
	free(k.fat);
	free(fat2);
	close(k.fd);
	return k.problems;
}
