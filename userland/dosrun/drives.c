/* The drive letters dosrun gives a DOS program (drives.h). The scheme
 * is ManiDOS's (userland/dos/drives.c): a drive letter names a ManiOS
 * directory, and a DOS path's backslashes become slashes. dosrun mounts
 * the same directories ManiDOS does, so a DOS program and ManiDOS see
 * the same files.
 */
#include "drives.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

struct mount {
	char letter;
	char root[64];
	int present;
};

static struct mount mounts[26];
static int current = 'C' - 'A';

void dos_mount(char letter, const char *dir)
{
	int i = toupper((unsigned char)letter) - 'A';
	if (i < 0 || i >= 26) {
		return;
	}
	mounts[i].letter = (char)toupper((unsigned char)letter);
	snprintf(mounts[i].root, sizeof(mounts[i].root), "%s", dir);
	mounts[i].present = 1;
}

/* The drives ManiDOS would mount: A: the boot disk, Z: ManiOS itself,
 * and C: onwards the disks under /n. dosrun mounts the same ones so a
 * program's paths line up with ManiDOS's. */
void dos_mount_defaults(void)
{
	dos_mount('A', "/boot");
	dos_mount('Z', "/");
	/* The disks under /n, in name order, as ManiDOS does. */
	char names[24][64];
	int n = 0;
	DIR *dir = opendir("/n");
	struct dirent *e;
	while (dir && n < 24 && (e = readdir(dir))) {
		if (e->d_type == DT_DIR) {
			snprintf(names[n++], sizeof(names[0]), "%s", e->d_name);
		}
	}
	if (dir) {
		closedir(dir);
	}
	/* A simple insertion sort by name. */
	for (int i = 1; i < n; i++) {
		char key[64];
		snprintf(key, sizeof(key), "%s", names[i]);
		int j = i - 1;
		while (j >= 0 && strcmp(names[j], key) > 0) {
			snprintf(names[j + 1], sizeof(names[0]), "%s", names[j]);
			j--;
		}
		snprintf(names[j + 1], sizeof(names[0]), "%s", key);
	}
	int letter = 'C';
	for (int i = 0; i < n && letter <= 'Y'; i++) {
		char root[80];
		snprintf(root, sizeof(root), "/n/%s", names[i]);
		dos_mount((char)letter++, root);
	}
}

int dos_path(char *path, size_t n)
{
	int d = current;
	char *in = path;
	if (isalpha((unsigned char)in[0]) && in[1] == ':') {
		d = toupper((unsigned char)in[0]) - 'A';
		in += 2;
	}
	if (d < 0 || d >= 26 || !mounts[d].present) {
		return -1;
	}
	/* The components, from the drive's root. */
	char work[512];
	snprintf(work, sizeof(work), "%s", in);
	char out[512];
	size_t len = 0;
	out[0] = '\0';
	char *save;
	for (char *p = strtok_r(work, "\\/", &save); p; p = strtok_r(NULL, "\\/", &save)) {
		if (!strcmp(p, ".")) {
			continue;
		}
		if (!strcmp(p, "..")) {
			char *slash = strrchr(out, '/');
			if (slash) {
				*slash = '\0';
				len = strlen(out);
			}
			continue;
		}
		len += (size_t)snprintf(out + len, sizeof(out) - len, "/%s", p);
	}
	/* The drive's root, then the path, lowercased. */
	snprintf(path, n, "%s%s", strcmp(mounts[d].root, "/") ? mounts[d].root : "", out);
	for (char *p = path; *p; p++) {
		*p = *p == '\\' ? '/' : (char)tolower((unsigned char)*p);
	}
	if (!path[0]) {
		snprintf(path, n, "/");
	}
	return 0;
}
