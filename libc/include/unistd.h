/* The POSIX names BSD programs use; ManiOS's own system calls are in
 * <manios.h>. */
#ifndef MANIOS_UNISTD_H
#define MANIOS_UNISTD_H

#include <sys/types.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* As in <manios.h>. */
long read(int fd, void *buf, size_t len);
long write(int fd, const void *buf, size_t len);
long lseek(int fd, long offset, int whence);
int close(int fd);

/* Options: POSIX getopt(), with opterr and optopt. */
extern char *optarg;
extern int optind, opterr, optopt;
int getopt(int argc, char *const argv[], const char *optstring);

/* OpenBSD's pledge() and unveil() restrict a process's system calls and
 * files. ManiOS has no such restriction yet (a namespace is its nearest
 * equivalent), so they do nothing and succeed: programs from OpenBSD keep
 * their calls unchanged. */
int pledge(const char *promises, const char *execpromises);
int unveil(const char *path, const char *permissions);

#endif
