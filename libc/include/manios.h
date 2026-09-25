/* The ManiOS system interface: the ZKT system calls (zkt_abi.h) as C
 * functions. On failure they return -1 and set errno, unless noted. */
#ifndef MANIOS_H
#define MANIOS_H

#include <stddef.h>
#include <stdint.h>
#include <zkt_abi.h>

__attribute__((noreturn)) void exit(int code);
__attribute__((noreturn)) void _exit(int code); /* exit() without library cleanup */

long read(int fd, void *buf, size_t len);   /* 0 at end of file */
long write(int fd, const void *buf, size_t len);
/* whence: SEEK_SET, SEEK_CUR, SEEK_END. Returns the new offset. */
long lseek(int fd, long offset, int whence);
int open(const char *path, int mode);       /* OREAD, OWRITE, ORDWR */
int close(int fd);
/* A connected pair: fds[0] and fds[1] each read what the other writes.
 * Each write is one message (a read returns bytes of one message only);
 * an empty write reads as end of file. */
int pipe(int fds[2]);
int dup(int fd);
int dup2(int fd, int newfd);
/* Waits up to timeout_ms (-1: forever) until a read of one of the
 * files would not block; sets each ready flag, returns how many. */
int poll(struct zkt_pollfd *fds, int count, int timeout_ms);
int fstat(int fd, struct zkt_dirent *out);

/* Starts the program at `path` with the NULL-terminated argv; it shares
 * this process's namespace and descriptors 0-2. Returns its pid. */
int spawn(const char *path, char *const argv[]);
/* Waits for child `pid`; stores its status (ZKT_WAIT_*) if non-NULL. */
int wait(int pid, int *status);
int getpid(void);
/* An exited child's pid (status stored), without waiting: 0 while all
 * are running, -1 with ECHILD when there are none. */
int reap(int *status);

int sleep_ms(uint32_t ms);
uint32_t uptime_ms(void); /* never fails */

/* Moves the end of the heap; returns the previous end, or (void *)-1.
 * malloc() uses it: other code should not, unless it never frees. */
void *sbrk(intptr_t increment);

/* Relative paths start from the current directory. getcwd returns buf,
 * or NULL (ERANGE if it is too small). */
int chdir(const char *path);
char *getcwd(char *buf, size_t size);

/* Namespaces (ADR-0003). flag: BIND_FLAG_REPLACE, _BEFORE or _AFTER. */
int bind(const char *new_path, const char *old_path, int flag);
int unbind(const char *old_path);
int nsfork(void); /* from now on, binds made here are private to this process */
/* Attaches to the ZRP server at `dial` ("udp!A.B.C.D!PORT", "udp!A.B.C.D"
 * or "A.B.C.D"; the port defaults to 5640) and binds its tree onto
 * old_path with `flag`, as bind() does. aname selects an export; NULL
 * or "" for the default. */
int mount(const char *dial, const char *old_path, int flag, const char *aname);
/* The same, with the ZRP server on the other end of pipe `fd`: how a
 * program serves files to others (zrpsrv.h does the serving). */
int mountfd(int fd, const char *old_path, int flag, const char *aname);
/* Serves the directory `path`, as this process sees it, to other
 * machines over ZRP as attach name `name` (mount DIAL OLD with that
 * aname reaches it), until the process exits or unexport(name). */
int export(const char *path, const char *name);
int unexport(const char *name);

/* A raw system call: returns the kernel's result (a negated error
 * number on failure) and leaves errno alone. */
long zkt_syscall(uint32_t num, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                 uint32_t a4);

#endif
