/* Drive letters and DOS paths (dos.h). ManiDOS's drives are places in
 * the ManiOS namespace:
 *
 *   A:        the boot disk: /boot, where the programs and AUTOEXEC.BAT are
 *   C: D: ... the FAT volumes ManiOS mounted at /n/ataXpY, in order
 *   Z:        all of ManiOS: /
 *
 * A DOS path is made absolute and upper case, then lower case for
 * ManiOS: FAT names are found whatever their case, and ManiOS's own
 * names are all lower case. */
#include "dos.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct drive drives[26];
int current;

char *upper(char *s)
{
	for (char *p = s; *p; p++) {
		*p = (char)toupper((unsigned char)*p);
	}
	return s;
}

static void add_drive(int letter, const char *root, const char *device, const char *kind)
{
	struct drive *d = &drives[letter - 'A'];
	d->present = true;
	strlcpy(d->root, root, sizeof(d->root));
	strlcpy(d->device, device, sizeof(d->device));
	d->kind = kind;
	strcpy(d->cwd, "\\");
}

static int by_name(const void *a, const void *b)
{
	return strcmp((const char *)a, (const char *)b);
}

void drives_init(void)
{
	add_drive('A', "/boot", "", "boot disk");
	add_drive('Z', "/", "", "ManiOS");
	/* The FAT volumes: directories under /n named after their disks. */
	char names[23][ZKT_NAME_MAX + 1];
	int n = 0, fd = open("/n", OREAD);
	struct zkt_dirent e;
	while (fd >= 0 && n < 23 && read(fd, &e, sizeof(e)) == sizeof(e)) {
		if (e.type == ZKT_TYPE_DIR && !strncmp(e.name, "ata", 3)) {
			strlcpy(names[n++], e.name, sizeof(names[0]));
		}
	}
	if (fd >= 0) {
		close(fd);
	}
	qsort(names, (size_t)n, sizeof(names[0]), by_name);
	for (int i = 0; i < n; i++) {
		char root[64], device[32];
		snprintf(root, sizeof(root), "/n/%s", names[i]);
		snprintf(device, sizeof(device), "/dev/%s", names[i]);
		add_drive('C' + i, root, device, "FAT");
	}
	current = drives['C' - 'A'].present ? 'C' - 'A' : 0;
}

void drives_follow_cwd(void)
{
	char cwd[256];
	if (!getcwd(cwd, sizeof(cwd))) {
		return;
	}
	/* The drive whose root is the longest prefix of the directory. */
	int best = -1;
	size_t best_len = 0;
	for (int i = 0; i < 26; i++) {
		const char *root = drives[i].root;
		size_t len = strlen(root);
		if (!drives[i].present || strncmp(cwd, root, len)
		    || (len > 1 && cwd[len] && cwd[len] != '/')) {
			continue;
		}
		if (best < 0 || len > best_len) {
			best = i;
			best_len = len;
		}
	}
	if (best < 0) {
		return;
	}
	current = best;
	char *rest = cwd + (best_len > 1 ? best_len : 0);
	char *d = drives[best].cwd;
	snprintf(d, PATH_MAX_DOS, "%s", *rest ? rest : "\\");
	for (char *p = d; *p; p++) {
		*p = *p == '/' ? '\\' : (char)toupper((unsigned char)*p);
	}
}

int dos_resolve(const char *in, int *drive, char *path)
{
	int d = current;
	if (isalpha((unsigned char)in[0]) && in[1] == ':') {
		d = toupper((unsigned char)in[0]) - 'A';
		in += 2;
	}
	if (!drives[d].present) {
		return -1;
	}
	/* Components onto a stack, from the root or the drive's directory. */
	char work[PATH_MAX_DOS * 2];
	if (in[0] == '\\' || in[0] == '/') {
		strlcpy(work, in, sizeof(work));
	} else {
		snprintf(work, sizeof(work), "%s\\%s", drives[d].cwd, in);
	}
	char *parts[64], *save;
	int n = 0;
	for (char *p = strtok_r(work, "\\/", &save); p; p = strtok_r(NULL, "\\/", &save)) {
		if (!strcmp(p, ".")) {
			continue;
		}
		if (!strcmp(p, "..")) {
			n -= n > 0;
			continue;
		}
		if (!strcmp(p, "...")) { /* DOS's "two up" */
			n -= n > 1 ? 2 : n;
			continue;
		}
		if (n < 64) {
			parts[n++] = p;
		}
	}
	path[0] = '\0';
	for (int i = 0; i < n; i++) {
		strlcat(path, "\\", PATH_MAX_DOS);
		strlcat(path, parts[i], PATH_MAX_DOS);
	}
	if (!path[0]) {
		strcpy(path, "\\");
	}
	upper(path);
	*drive = d;
	return 0;
}

void dos_to_manios(int drive, const char *path, char *out, size_t size)
{
	const char *root = drives[drive].root;
	snprintf(out, size, "%s%s", strcmp(root, "/") ? root : "", path);
	for (char *p = out + strlen(strcmp(root, "/") ? root : ""); *p; p++) {
		*p = *p == '\\' ? '/' : (char)tolower((unsigned char)*p);
	}
	if (!out[0]) {
		strlcpy(out, "/", size);
	}
}

void dos_display(int drive, const char *path, char *out, size_t size)
{
	snprintf(out, size, "%c:%s", 'A' + drive, path);
}

int dos_manios(const char *in, char *out, size_t size)
{
	int d;
	char path[PATH_MAX_DOS];
	if (dos_resolve(in, &d, path) < 0) {
		return -1;
	}
	dos_to_manios(d, path, out, size);
	return 0;
}

int dos_type(const char *manios_path, uint32_t *size)
{
	int fd = open(manios_path, OREAD);
	if (fd < 0) {
		return -1;
	}
	struct zkt_dirent e;
	int rc = fstat(fd, &e);
	close(fd);
	if (rc < 0) {
		return -1;
	}
	if (size) {
		*size = e.size;
	}
	return (int)e.type;
}

bool has_wild(const char *s)
{
	return strpbrk(s, "*?") != NULL;
}

static bool match_here(const char *p, const char *n)
{
	for (; *p; p++, n++) {
		if (*p == '*') {
			for (;; n++) {
				if (match_here(p + 1, n)) {
					return true;
				}
				if (!*n) {
					return false;
				}
			}
		}
		if (!*n || (*p != '?' && toupper((unsigned char)*p) != toupper((unsigned char)*n))) {
			return false;
		}
	}
	return !*n;
}

bool wild_match(const char *pattern, const char *name)
{
	/* "*.*" is everything, names without a dot too, as in DOS; and
	 * "NAME." is a name with no extension. */
	if (!strcmp(pattern, "*.*")) {
		return true;
	}
	size_t len = strlen(pattern);
	if (len > 1 && pattern[len - 1] == '.' && pattern[len - 2] != '.') {
		char p[PATH_MAX_DOS];
		strlcpy(p, pattern, sizeof(p));
		p[len - 1] = '\0';
		return !strchr(name, '.') && match_here(p, name);
	}
	return match_here(pattern, name);
}

int each_entry(const char *manios_dir, entry_fn fn, void *arg)
{
	int fd = open(manios_dir, OREAD);
	if (fd < 0) {
		return -1;
	}
	struct zkt_dirent e;
	if (fstat(fd, &e) < 0 || e.type != ZKT_TYPE_DIR) {
		close(fd);
		return -1;
	}
	while (read(fd, &e, sizeof(e)) == sizeof(e)) {
		if (strcmp(e.name, ".") && strcmp(e.name, "..")) {
			fn(&e, arg);
		}
	}
	close(fd);
	return 0;
}

void split_last(const char *in, char *dir, size_t dsize, char *name, size_t nsize)
{
	const char *cut = NULL;
	for (const char *p = in; *p; p++) {
		if (*p == '\\' || *p == '/' || (p == in + 1 && *p == ':')) {
			cut = p;
		}
	}
	if (!cut) {
		strlcpy(dir, "", dsize);
		strlcpy(name, in, nsize);
		return;
	}
	size_t n = (size_t)(cut - in) + 1;
	/* Keep the separator for "C:" and "\" (the root), drop it otherwise. */
	if (n > 1 && *cut != ':' && !(n == 3 && in[1] == ':')) {
		n--;
	}
	if (n >= dsize) {
		n = dsize - 1;
	}
	memcpy(dir, in, n);
	dir[n] = '\0';
	strlcpy(name, cut + 1, nsize);
}
