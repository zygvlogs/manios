/* The ZKT userspace ABI: shared, unchanged, by the kernel and by
 * userspace (libc). Anything here is a promise to existing binaries;
 * see docs/milestones/M8-userspace.md. */
#ifndef ZKT_ABI_H
#define ZKT_ABI_H

#include <stdint.h>

/* System calls: `int $0x80` with the number in EAX and up to five
 * arguments in EBX, ECX, EDX, ESI, EDI. The result comes back in EAX:
 * >= 0 on success, or a negated error number (-ENOENT). */
#define SYS_EXIT    0  /* (int code) -- does not return */
#define SYS_READ    1  /* (int fd, void *buf, uint32_t len) -> bytes */
#define SYS_WRITE   2  /* (int fd, const void *buf, uint32_t len) -> bytes */
#define SYS_OPEN    3  /* (const char *path, int mode) -> fd */
#define SYS_CLOSE   4  /* (int fd) */
#define SYS_SPAWN   5  /* (const char *path, char *const argv[]) -> pid */
#define SYS_WAIT    6  /* (int pid, int *status) -> pid */
#define SYS_GETPID  7  /* () -> pid */
#define SYS_SLEEP   8  /* (uint32_t ms) */
#define SYS_BIND    9  /* (const char *new, const char *old, int flag) */
#define SYS_UNBIND  10 /* (const char *old) */
#define SYS_NSFORK  11 /* () -- give this process a private copy of its namespace */
#define SYS_FSTAT   12 /* (int fd, struct zkt_dirent *out) */
#define SYS_UPTIME  13 /* () -> milliseconds since boot, modulo 2^31 (24.8 days) */

/* Limits. Descriptors 0-2 are standard input, output and error. */
#define ZKT_FD_MAX     16   /* open descriptors per process */
#define ZKT_ARGS_MAX   16   /* SYS_SPAWN arguments, argv[0] included */
#define ZKT_ARG_BYTES  1024 /* SYS_SPAWN argument strings, NULs included */

/* Open modes. */
#define OREAD  0
#define OWRITE 1
#define ORDWR  2

/* SYS_BIND flags, as Plan 9's bind: replace, or union with the new
 * directory searched before (-b) or after (-a) the old one. */
#define BIND_FLAG_REPLACE 0
#define BIND_FLAG_BEFORE  1
#define BIND_FLAG_AFTER   2

/* Reading a directory returns whole records of this type; SYS_FSTAT
 * fills one for an open file (name is the last path element). */
#define ZKT_NAME_MAX 63
#define ZKT_TYPE_DIR    0
#define ZKT_TYPE_FILE   1
#define ZKT_TYPE_DEVICE 2
struct zkt_dirent {
	char name[ZKT_NAME_MAX + 1];
	uint32_t type;
	uint32_t size;
};

/* SYS_WAIT status: an exit code 0-255, or ZKT_WAIT_KILLED plus the CPU
 * exception vector that killed the process. */
#define ZKT_WAIT_KILLED 0x100
#define ZKT_WAIT_VECTOR(s) ((s) & 0xFF)
#define ZKT_WAIT_CODE(s) ((s) & 0xFF)

/* At process entry, ESP points at argc (uint32_t), followed by argv
 * (char **, a NULL-terminated array). */

/* Error numbers (returned negated). Traditional Unix values. */
#define EPERM         1
#define ENOENT        2
#define E2BIG         7
#define ENOEXEC       8
#define EBADF         9
#define ECHILD       10
#define EIO           5
#define ENXIO         6
#define ENOMEM       12
#define EFAULT       14
#define EBUSY        16
#define EEXIST       17
#define ENODEV       19
#define ENOTDIR      20
#define EISDIR       21
#define EINVAL       22
#define EMFILE       24
#define EROFS        30
#define ENAMETOOLONG 36
#define ENOSYS       38

#endif
