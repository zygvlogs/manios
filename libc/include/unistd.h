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
int dup(int fd);
int dup2(int fd, int newfd);
int pipe(int fds[2]);
int getpid(void);

/* access() modes. ManiOS has no permissions: what exists can be read;
 * writing is tried (only devices and pipes take it); only directories
 * can be "executed" (searched). */
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
int access(const char *path, int mode);
int isatty(int fd); /* the console, /dev/cons */

/* Everyone is user and group 0. */
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);

/* Files can't be removed or linked, nor owners changed: EROFS. */
int unlink(const char *path);
int rmdir(const char *path);
int link(const char *from, const char *to);
int chown(const char *path, uid_t uid, gid_t gid);
int fchown(int fd, uid_t uid, gid_t gid);

/* The current directory, as in <manios.h>. */
int chdir(const char *path);
char *getcwd(char *buf, size_t size);

unsigned int sleep(unsigned int seconds);
int getpagesize(void); /* 4096 */
/* ManiOS has no working directory by descriptor: -1, ENOSYS. */
int fchdir(int fd);
__dead void _exit(int code); /* exit() without library cleanup */

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
