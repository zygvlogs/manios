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
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

struct mount {
	char letter;
	char root[64];
	char cwd[64]; /* relative to the drive root, without a leading slash */
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
	mounts[i].cwd[0] = 0;
}

int dos_get_drive(void)
{
	return current;
}

int dos_set_drive(int drive)
{
	if (drive < 0 || drive >= 26 || !mounts[drive].present) {
		return -1; /* DOS reports invalid drive (AH=15h) */
	}
	current = drive;
	return 0;
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
	/* A ManiOS absolute path (e.g. /n/cd0/install.exe) is used as-is.
	 * This happens when dosrun is invoked from the ManiOS shell, or
	 * when ManiDOS has already translated a DOS path for it. */
	if (in == path && in[0] == '/') {
		(void)n;
		return 0;
	}
	/* An unmounted drive is no drive: mapping it onto C: would quietly
	 * read and write the wrong disk. */
	if (d < 0 || d >= 26 || !mounts[d].present) {
		return -1;
	}
	/* The components, from the drive's root. */
	char work[512];
	if (*in != '/' && *in != '\\' && mounts[d].cwd[0]) {
		if (snprintf(work, sizeof(work), "%s/%s", mounts[d].cwd, in) >= (int)sizeof(work)) return -1;
	} else {
		if (strlen(in) >= sizeof(work)) return -1;
		snprintf(work, sizeof(work), "%s", in);
	}
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
		/* Room for "/%s" and the NUL: if the component doesn't fit,
		 * stop rather than keep a half-truncated name that would open
		 * the wrong file. */
		if (len + 2 + strlen(p) >= sizeof(out)) {
			return -1;
		}
		len += (size_t)snprintf(out + len, sizeof(out) - len, "/%s", p);
	}
	/* The whole result must fit the caller's buffer too; snprintf
	 * reports how much it would have needed. */
	{
		int need = snprintf(NULL, 0, "%s%s",
		                    strcmp(mounts[d].root, "/") ? mounts[d].root : "", out);
		if (need < 0 || (size_t)need >= n) {
			return -1;
		}
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

int dos_guest_path(const char *host, char *out, size_t n)
{
	int best = -1;
	size_t prefix = 0;
	for (int i = 0; i < 26; i++) {
		if (!mounts[i].present) continue;
		size_t len = strlen(mounts[i].root);
		while (len > 1 && mounts[i].root[len - 1] == '/') len--;
		if (strncmp(host, mounts[i].root, len) ||
		    ((len != 1 || mounts[i].root[0] != '/') && host[len] && host[len] != '/')) continue;
		if (len > prefix) { best = i; prefix = len; }
	}
	if (best < 0) return -1;
	const char *relative = host + prefix;
	while (*relative == '/') relative++;
	if (strlen(relative) + 4 > n) return -1;
	out[0] = 'A' + best; out[1] = ':'; out[2] = '\\';
	size_t i;
	for (i = 0; relative[i]; i++) out[3 + i] = relative[i] == '/' ? '\\' : relative[i];
	out[3 + i] = 0;
	return 0;
}

int dos_chdir(const char *path)
{
	int drive = current;
	if (isalpha((unsigned char)path[0]) && path[1] == ':')
		drive = toupper((unsigned char)path[0]) - 'A';
	if (drive < 0 || drive >= 26 || !mounts[drive].present) return -1;
	char mapped[512];
	if (strlen(path) >= sizeof(mapped)) return -1;
	snprintf(mapped, sizeof(mapped), "%s", path);
	if (dos_path(mapped, sizeof(mapped)) < 0) return -1;
	struct stat st;
	if (stat(mapped, &st) < 0 || !S_ISDIR(st.st_mode)) return -1;
	size_t rootlen = strlen(mounts[drive].root);
	if (strncmp(mapped, mounts[drive].root, rootlen) ||
	    (strcmp(mounts[drive].root, "/") && mapped[rootlen] && mapped[rootlen] != '/')) return -1;
	const char *relative = mapped + rootlen;
	while (*relative == '/') relative++;
	if (strlen(relative) >= sizeof(mounts[drive].cwd)) return -1;
	snprintf(mounts[drive].cwd, sizeof(mounts[drive].cwd), "%s", relative);
	return 0;
}

int dos_getcwd(int drive, char *out, size_t n)
{
	if (drive < 0 || drive >= 26 || !mounts[drive].present || strlen(mounts[drive].cwd) >= n) return -1;
	snprintf(out, n, "%s", mounts[drive].cwd);
	for (char *p = out; *p; p++) *p = *p == '/' ? '\\' : (char)toupper((unsigned char)*p);
	return 0;
}

int dos_directory(const char *path, int remove)
{
	char mapped[512];
	int explicit_drive = isalpha((unsigned char)path[0]) && path[1] == ':';
	if (!*path || strchr(path, '*') || strchr(path, '?') ||
	    strchr(path + (explicit_drive ? 2 : 0), ':')) { errno = ENOENT; return -1; }
	int drive = explicit_drive ? toupper((unsigned char)path[0]) - 'A' : current;
	if (drive < 0 || drive >= 26 || !mounts[drive].present) { errno = ENOENT; return -1; }
	/* DS:DX names a DOS path: /DIR is rooted on the default drive, not
	 * an escape into the host namespace. Preserve drive-relative paths. */
	int n = explicit_drive ? snprintf(mapped, sizeof(mapped), "%s", path) :
	        snprintf(mapped, sizeof(mapped), "%c:%s", 'A' + drive, path);
	if (n < 0 || n >= (int)sizeof(mapped) || dos_path(mapped, sizeof(mapped)) < 0) {
		errno = ENOENT; return -1;
	}
	if (remove) {
		for (int i = 0; i < 26; i++) if (mounts[i].present) {
			if (!strcmp(mapped, mounts[i].root)) { errno = EACCES; return -1; }
			char cwd[512];
			snprintf(cwd, sizeof(cwd), "%s%s%s", mounts[i].root,
			         mounts[i].cwd[0] && strcmp(mounts[i].root, "/") ? "/" : "", mounts[i].cwd);
			if (!strcmp(mapped, cwd)) { errno = EBUSY; return -1; }
		}
		return rmdir(mapped);
	}
	return mkdir(mapped, 0777);
}
