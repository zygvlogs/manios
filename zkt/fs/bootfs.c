/* ustar parsing per POSIX.1-2001 (pax "ustar" interchange format). */
#include "bootfs.h"
#include <stdbool.h>
#include <stdint.h>
#include "kprintf.h"
#include "kstring.h"
#include "ramfs.h"

#define BLOCK 512
#define TYPE_FILE     '0'
#define TYPE_FILE_OLD '\0'
#define TYPE_DIR      '5'

/* objcopy -I binary names these after the input file (Makefile). */
extern const uint8_t _binary_bootfs_tar_start[];
extern const uint8_t _binary_bootfs_tar_end[];

static bool parse_octal(const uint8_t *field, size_t len, uint32_t *out)
{
	uint32_t v = 0;
	size_t i = 0;
	while (i < len && field[i] == ' ') {
		i++;
	}
	for (; i < len && field[i] >= '0' && field[i] <= '7'; i++) {
		if (v > 0x1FFFFFFFu) {
			return false;
		}
		v = v * 8 + (uint32_t)(field[i] - '0');
	}
	*out = v;
	return i == len || field[i] == '\0' || field[i] == ' ';
}

/* The header checksum counts its own field as eight spaces. */
static bool checksum_ok(const uint8_t *h)
{
	uint32_t stored;
	if (!parse_octal(h + 148, 8, &stored)) {
		return false;
	}
	uint32_t sum = 0;
	for (int i = 0; i < BLOCK; i++) {
		sum += (i >= 148 && i < 156) ? ' ' : h[i];
	}
	return sum == stored;
}

/* "prefix/name", without a leading "./" or "/". */
static bool entry_path(const uint8_t *h, char *out, size_t size)
{
	char name[101], prefix[156];
	memcpy(name, h, 100);
	name[100] = '\0';
	memcpy(prefix, h + 345, 155);
	prefix[155] = '\0';
	int n = prefix[0] ? ksnprintf(out, size, "%s/%s", prefix, name)
	                  : ksnprintf(out, size, "%s", name);
	if (n < 0 || (size_t)n >= size) {
		return false;
	}
	char *p = out;
	while (*p == '.' && p[1] == '/') {
		p += 2;
	}
	while (*p == '/') {
		p++;
	}
	memmove(out, p, strlen(p) + 1);
	return true;
}

struct vnode *bootfs_create(void)
{
	struct vnode *root = ramfs_create();
	if (!root) {
		return 0;
	}
	const uint8_t *p = _binary_bootfs_tar_start;
	const uint8_t *end = _binary_bootfs_tar_end;

	while (end - p >= BLOCK && p[0] != '\0') {
		char path[VFS_PATH_MAX + 1];
		uint32_t size;
		if (memcmp(p + 257, "ustar", 5) != 0 || !checksum_ok(p)
		    || !parse_octal(p + 124, 12, &size) || !entry_path(p, path, sizeof(path))
		    || size > (uint32_t)(end - p - BLOCK)) {
			kprintf("bootfs: malformed archive\n");
			ramfs_destroy(root);
			return 0;
		}
		uint8_t type = p[156];
		if (path[0] && (type == TYPE_FILE || type == TYPE_FILE_OLD)) {
			ramfs_add_file(root, path, p + BLOCK, size);
		} else if (path[0] && type == TYPE_DIR) {
			ramfs_mkdir(root, path);
		}
		p += BLOCK + ((size + BLOCK - 1) / BLOCK) * BLOCK;
	}
	return root;
}
