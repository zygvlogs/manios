/* POSIX open() and fcntl(). ManiOS opens existing files only: O_CREAT
 * of a file that isn't there fails (EROFS), and O_TRUNC is refused.
 * ManiOS's own open() (<manios.h>) takes OREAD/OWRITE/ORDWR, which are
 * O_RDONLY, O_WRONLY and O_RDWR; this one, under another link name,
 * also takes the flags below. */
#ifndef MANIOS_FCNTL_H
#define MANIOS_FCNTL_H

#include <sys/types.h>

#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_ACCMODE   3
#define O_NONBLOCK  0x0004
#define O_APPEND    0x0008
#define O_CREAT     0x0200
#define O_TRUNC     0x0400
#define O_EXCL      0x0800
#define O_NOFOLLOW  0x0100
#define O_CLOEXEC   0x10000
#define O_DIRECTORY 0x20000

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define FD_CLOEXEC 1

/* For the *at() functions: the current directory, and not following a
 * last link (ManiOS has no links). */
#define AT_FDCWD            -100
#define AT_SYMLINK_NOFOLLOW 0x02

int open(const char *path, int flags, ...) __asm__("__posix_open");
/* F_DUPFD duplicates; F_SETFD and F_SETFL are accepted and ignored
 * (ManiOS has no exec, and nothing is non-blocking); F_GETFL fails
 * (ENOSYS): ManiOS doesn't say how a descriptor was opened. */
int fcntl(int fd, int cmd, ...);

#endif
