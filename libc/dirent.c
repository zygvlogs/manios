/* opendir() and readdir() over ManiOS's directory reads, which return
 * whole struct zkt_dirent records. */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int manios_open(const char *path, int mode) __asm__("open");
int manios_fstat(int fd, struct zkt_dirent *out) __asm__("fstat");
int __manios_abspath(const char *path, char *out, size_t size); /* stat.c */
uint32_t __manios_path_ino(const char *abspath, uint32_t type);


#define BATCH 16

struct manios_dir {
	int fd;
	char *path;      /* absolute, for d_fileno and fstatat(); NULL from fdopendir */
	int count, next; /* records in batch, and the next to hand out */
	off_t offset;    /* of the next record, for d_off */
	struct zkt_dirent batch[BATCH];
	struct dirent entry;
};

/* The open directories, by descriptor, for fstatat() (stat.c). */
static DIR *open_dirs[ZKT_FD_MAX];

const char *__manios_dir_path(int fd);

const char *__manios_dir_path(int fd)
{
	return fd >= 0 && fd < ZKT_FD_MAX && open_dirs[fd] ? open_dirs[fd]->path : NULL;
}

DIR *fdopendir(int fd)
{
	struct zkt_dirent self;
	if (manios_fstat(fd, &self) < 0) {
		return NULL;
	}
	if (self.type != ZKT_TYPE_DIR) {
		errno = ENOTDIR;
		return NULL;
	}
	DIR *dir = calloc(1, sizeof(*dir));
	if (!dir) {
		return NULL;
	}
	dir->fd = fd;
	if (fd >= 0 && fd < ZKT_FD_MAX) {
		open_dirs[fd] = dir;
	}
	return dir;
}

DIR *opendir(const char *path)
{
	int fd = manios_open(path, OREAD);
	if (fd < 0) {
		return NULL;
	}
	DIR *dir = fdopendir(fd);
	char abs[PATH_MAX];
	if (dir && __manios_abspath(path, abs, sizeof(abs)) == 0) {
		dir->path = strdup(abs);
	}
	if (!dir) {
		int saved = errno;
		close(fd);
		errno = saved;
	}
	return dir;
}

struct dirent *readdir(DIR *dir)
{
	if (dir->next == dir->count) {
		long n = read(dir->fd, dir->batch, sizeof(dir->batch));
		if (n <= 0) {
			return NULL; /* the end (errno says if it was an error) */
		}
		dir->count = (int)(n / (long)sizeof(struct zkt_dirent));
		dir->next = 0;
	}
	const struct zkt_dirent *d = &dir->batch[dir->next++];
	struct dirent *e = &dir->entry;
	size_t len = strnlen(d->name, MAXNAMLEN);
	memcpy(e->d_name, d->name, len);
	e->d_name[len] = '\0';
	e->d_namlen = (uint8_t)len;
	e->d_type = d->type == ZKT_TYPE_DIR ? DT_DIR : d->type == ZKT_TYPE_DEVICE ? DT_CHR
	          : d->type == ZKT_TYPE_PIPE ? DT_FIFO : DT_REG;
	/* As stat() numbers it: from the whole path, when it is known. */
	char full[PATH_MAX];
	if (dir->path && (size_t)snprintf(full, sizeof(full), "%s/%s",
	                                  strcmp(dir->path, "/") ? dir->path : "", e->d_name)
	                         < sizeof(full)) {
		e->d_fileno = __manios_path_ino(full, d->type);
	} else {
		e->d_fileno = 0;
	}
	e->d_reclen = sizeof(*e);
	dir->offset += sizeof(struct zkt_dirent);
	e->d_off = dir->offset;
	return e;
}

void rewinddir(DIR *dir)
{
	lseek(dir->fd, 0, SEEK_SET);
	dir->count = dir->next = 0;
	dir->offset = 0;
}

int closedir(DIR *dir)
{
	if (dir->fd >= 0 && dir->fd < ZKT_FD_MAX && open_dirs[dir->fd] == dir) {
		open_dirs[dir->fd] = NULL;
	}
	int rc = close(dir->fd);
	free(dir->path);
	free(dir);
	return rc;
}

int dirfd(DIR *dir)
{
	return dir->fd;
}
