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
int open(const char *path, int mode);       /* OREAD, OWRITE, ORDWR */
int close(int fd);
int fstat(int fd, struct zkt_dirent *out);

/* Starts the program at `path` with the NULL-terminated argv; it shares
 * this process's namespace and descriptors 0-2. Returns its pid. */
int spawn(const char *path, char *const argv[]);
/* Waits for child `pid`; stores its status (ZKT_WAIT_*) if non-NULL. */
int wait(int pid, int *status);
int getpid(void);

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

/* A raw system call: returns the kernel's result (a negated error
 * number on failure) and leaves errno alone. */
long zkt_syscall(uint32_t num, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                 uint32_t a4);

#endif
