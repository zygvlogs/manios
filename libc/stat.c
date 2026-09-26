/* POSIX file status and opening (<sys/stat.h>, <fcntl.h>, access(),
 * isatty()) over ManiOS's open() and fstat(). Regular files can't be
 * written, created or changed in ManiOS: only devices and pipes take
 * writes. */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zkt_abi.h>

/* ManiOS's own calls; <manios.h> declares them under these names, which
 * the POSIX ones above take over. */
int manios_open(const char *path, int mode) __asm__("open");
int manios_fstat(int fd, struct zkt_dirent *out) __asm__("fstat");

/* A file's number: FNV-1a of what names it, then its type. */
static uint32_t hash_name(const char *s, size_t len, uint32_t type)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < len; i++) {
		h = (h ^ (uint8_t)s[i]) * 16777619u;
	}
	h = (h ^ type) * 16777619u;
	return h ? h : 1;
}

int __manios_abspath(const char *path, char *out, size_t size);

/* path made absolute (from the current directory) and cleaned as the
 * kernel cleans paths: no ".", "..", or doubled slashes. */
int __manios_abspath(const char *path, char *out, size_t size)
{
	char joined[2 * PATH_MAX];
	if (path[0] == '/') {
		strlcpy(joined, path, sizeof(joined));
	} else {
		if (!getcwd(joined, PATH_MAX)) {
			return -1;
		}
		strlcat(joined, "/", sizeof(joined));
		strlcat(joined, path, sizeof(joined));
	}
	size_t len = 0;
	for (char *p = joined; *p;) {
		while (*p == '/') {
			p++;
		}
		char *end = p + strcspn(p, "/");
		size_t n = (size_t)(end - p);
		if (n == 0 || (n == 1 && p[0] == '.')) {
			/* nothing */
		} else if (n == 2 && p[0] == '.' && p[1] == '.') {
			while (len > 0 && out[--len] != '/') {
			}
		} else {
			if (len + 1 + n + 1 > size) {
				errno = ENAMETOOLONG;
				return -1;
			}
			out[len++] = '/';
			memcpy(out + len, p, n);
			len += n;
		}
		p = end;
	}
	if (len == 0) {
		out[len++] = '/';
	}
	out[len] = '\0';
	return 0;
}

static void fill(struct stat *sb, const struct zkt_dirent *d)
{
	static const mode_t modes[] = {
		[ZKT_TYPE_DIR] = S_IFDIR | 0755,
		[ZKT_TYPE_FILE] = S_IFREG | 0644,
		[ZKT_TYPE_DEVICE] = S_IFCHR | 0666,
		[ZKT_TYPE_PIPE] = S_IFIFO | 0600,
	};
	memset(sb, 0, sizeof(*sb));
	sb->st_mode = d->type <= ZKT_TYPE_PIPE ? modes[d->type] : S_IFREG | 0644;
	sb->st_ino = hash_name(d->name, strlen(d->name), d->type);
	/* 1, as the BSDs report on file systems that keep no link counts:
	 * a directory's 2 would tell fts it has no subdirectories. */
	sb->st_nlink = 1;
	sb->st_size = (off_t)d->size;
	sb->st_blksize = 4096;
	sb->st_blocks = (d->size + 511) / 512;
}

int __posix_fstat(int fd, struct stat *sb);

int __posix_fstat(int fd, struct stat *sb)
{
	struct zkt_dirent d;
	if (manios_fstat(fd, &d) < 0) {
		return -1;
	}
	fill(sb, &d);
	return 0;
}

uint32_t __manios_path_ino(const char *abspath, uint32_t type);

uint32_t __manios_path_ino(const char *abspath, uint32_t type)
{
	return hash_name(abspath, strlen(abspath), type);
}

/* By path, st_ino comes from the whole path: files with the same name in
 * different directories differ (fstat() has only the name). */
int stat(const char *path, struct stat *sb)
{
	char abs[PATH_MAX];
	struct zkt_dirent d;
	if (__manios_abspath(path, abs, sizeof(abs)) < 0) {
		return -1;
	}
	int fd = manios_open(path, OREAD);
	if (fd < 0) {
		return -1;
	}
	int rc = manios_fstat(fd, &d);
	close(fd);
	if (rc < 0) {
		return -1;
	}
	fill(sb, &d);
	sb->st_ino = __manios_path_ino(abs, d.type);
	return 0;
}

int lstat(const char *path, struct stat *sb)
{
	return stat(path, sb); /* no symbolic links */
}

const char *__manios_dir_path(int fd); /* dirent.c */

int fstatat(int dirfd, const char *path, struct stat *sb, int flag)
{
	(void)flag;
	if (dirfd == AT_FDCWD || path[0] == '/') {
		return stat(path, sb);
	}
	/* Relative to a directory opendir() opened: its path is known. */
	const char *dir = __manios_dir_path(dirfd);
	char full[PATH_MAX];
	if (!dir) {
		errno = ENOSYS;
		return -1;
	}
	if ((size_t)snprintf(full, sizeof(full), "%s/%s", dir, path) >= sizeof(full)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return stat(full, sb);
}

int chmod(const char *path, mode_t mode)
{
	struct stat sb;
	(void)mode;
	if (stat(path, &sb) == 0) {
		errno = EROFS;
	}
	return -1;
}

int fchmod(int fd, mode_t mode)
{
	(void)fd;
	(void)mode;
	errno = EROFS;
	return -1;
}

int mkdir(const char *path, mode_t mode)
{
	struct stat sb;
	(void)mode;
	errno = stat(path, &sb) == 0 ? EEXIST : EROFS;
	return -1;
}

static mode_t mask = 022;

mode_t umask(mode_t m)
{
	mode_t old = mask;
	mask = m & 0777;
	return old;
}

int __posix_open(const char *path, int flags, ...);

int __posix_open(const char *path, int flags, ...)
{
	int fd = manios_open(path, flags & O_ACCMODE);
	if (fd < 0) {
		if (errno == ENOENT && (flags & O_CREAT)) {
			errno = EROFS; /* ManiOS can't create files */
		}
		return -1;
	}
	struct stat sb;
	if (__posix_fstat(fd, &sb) < 0) {
		close(fd);
		return -1;
	}
	int err = 0;
	if ((flags & O_CREAT) && (flags & O_EXCL)) {
		err = EEXIST;
	} else if ((flags & O_DIRECTORY) && !S_ISDIR(sb.st_mode)) {
		err = ENOTDIR;
	} else if ((flags & O_TRUNC) && S_ISREG(sb.st_mode)) {
		err = EROFS; /* a regular file can't be shortened */
	}
	if (err) {
		close(fd);
		errno = err;
		return -1;
	}
	if (flags & O_APPEND) {
		lseek(fd, 0, SEEK_END);
	}
	return fd;
}

int fcntl(int fd, int cmd, ...)
{
	va_list ap;
	va_start(ap, cmd);
	int arg = va_arg(ap, int);
	va_end(ap);
	struct stat sb;
	if (__posix_fstat(fd, &sb) < 0) {
		return -1; /* EBADF */
	}
	switch (cmd) {
	case F_DUPFD:
		for (int n = arg; n < ZKT_FD_MAX; n++) {
			struct stat other;
			if (__posix_fstat(n, &other) < 0) {
				return dup2(fd, n);
			}
		}
		errno = EMFILE;
		return -1;
	case F_GETFD:
	case F_SETFD:
	case F_SETFL:
		return 0;
	case F_GETFL:
		errno = ENOSYS; /* ManiOS doesn't say how a descriptor was opened */
		return -1;
	default:
		errno = EINVAL;
		return -1;
	}
}

int access(const char *path, int mode)
{
	struct stat sb;
	if (stat(path, &sb) < 0) {
		return -1;
	}
	if ((mode & X_OK) && !S_ISDIR(sb.st_mode)) {
		errno = EACCES; /* files have no execute permission to give */
		return -1;
	}
	if (mode & W_OK) {
		int fd = manios_open(path, OWRITE);
		if (fd < 0) {
			return -1;
		}
		close(fd);
	}
	return 0;
}

/* The console is ManiOS's only terminal. */
int isatty(int fd)
{
	struct zkt_dirent d;
	if (manios_fstat(fd, &d) < 0) {
		return 0;
	}
	if (d.type != ZKT_TYPE_DEVICE || strcmp(d.name, "cons") != 0) {
		errno = ENOTTY;
		return 0;
	}
	return 1;
}
