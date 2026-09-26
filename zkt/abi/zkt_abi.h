/* The ZKT userspace ABI: shared, unchanged, by the kernel and by
 * userspace (libc). Anything here is a promise to existing binaries;
 * see docs/milestones/M8-userspace.md and M9-libc-shell.md. Usable from
 * assembly too (the constants; not the structures). */
#ifndef ZKT_ABI_H
#define ZKT_ABI_H

/* Every executable carries an ELF note naming the ABI version it was
 * built for: in a PT_NOTE segment, name "ZKT", type ZKT_NOTE_ABI, and a
 * 4-byte descriptor holding the version. The kernel refuses (ENOEXEC)
 * executables without it, or built for a version it doesn't know.
 * Version 1: the system calls below. New calls may be added under the
 * same version; changing an existing one means a new version. */
#define ZKT_ABI_VERSION 1
#define ZKT_NOTE_NAME "ZKT"
#define ZKT_NOTE_ABI 1

/* System calls: `int $0x80` with the number in EAX and up to five
 * arguments in EBX, ECX, EDX, ESI, EDI. The result comes back in EAX: a
 * negated error number (-ENOENT) from -1 to -4095, or else a result.
 * (Addresses above 2 GiB are negative as signed numbers; only that
 * range means failure.) */
#define ZKT_ERRNO_MAX 4095
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
#define SYS_SBRK    14 /* (int32_t increment) -> the previous end of the heap */
#define SYS_CHDIR   15 /* (const char *path) */
#define SYS_GETCWD  16 /* (char *buf, uint32_t len) -> length, without the NUL */
#define SYS_MOUNT   17 /* (const char *dial, const char *old, int flag, const char *aname):
                        * attach to the ZRP server at dial ("udp!A.B.C.D!PORT") and bind
                        * its tree onto old, as SYS_BIND does. aname may be NULL. */
#define SYS_SEEK    18 /* (int fd, int32_t offset, int whence) -> the new offset. A
                        * directory can only be rewound (offset 0, SEEK_SET); a
                        * pipe can't be seeked at all (ESPIPE, since 0.17). */
#define SYS_POLL    19 /* (struct zkt_pollfd *fds, uint32_t count, int32_t timeout_ms)
                        * -> how many are readable (0: timed out). -1 waits forever. */
#define SYS_PIPE    20 /* (int fds[2]): a connected pair; each write is one message */
#define SYS_DUP     21 /* (int fd) -> the lowest free descriptor, sharing fd's file */
#define SYS_DUP2    22 /* (int fd, int newfd) -> newfd, closing what it held */
#define SYS_REAP    23 /* (int *status) -> pid of an exited child, 0 if none has exited
                        * yet, -ECHILD if there are no children; never waits */
#define SYS_MOUNTFD 24 /* (int fd, const char *old, int flag, const char *aname): as
                        * SYS_MOUNT, with a ZRP server on the other end of pipe fd --
                        * how a userspace program serves files (M12) */
#define SYS_EXPORT  25 /* (const char *path, const char *name): serves the directory
                        * path, in this process's namespace, over ZRP as attach name
                        * `name` until the process exits; a NULL path takes the
                        * export back (M13) */

/* Paths may be relative to the process's current directory ("/" for a
 * program the kernel starts; a child inherits its parent's). They are
 * cleaned lexically, as Plan 9 does: ".." never escapes a bind. */

/* Limits. Descriptors 0-2 are standard input, output and error. */
#define ZKT_FD_MAX     16   /* open descriptors per process */
#define ZKT_ARGS_MAX   16   /* SYS_SPAWN arguments, argv[0] included */
#define ZKT_ARG_BYTES  1024 /* SYS_SPAWN argument strings, NULs included */
#define ZKT_PATH_MAX   255  /* bytes in a path, without the NUL */

/* /dev/kbd bytes (zkt/drivers/input.c): ASCII, with Ctrl+letter as a
 * control character, or one of these for keys ASCII has no code for. */
#define ZKT_KEY_UP     0x80
#define ZKT_KEY_DOWN   0x81
#define ZKT_KEY_LEFT   0x82
#define ZKT_KEY_RIGHT  0x83
#define ZKT_KEY_HOME   0x84
#define ZKT_KEY_END    0x85
#define ZKT_KEY_PGUP   0x86
#define ZKT_KEY_PGDN   0x87
#define ZKT_KEY_INSERT 0x88
#define ZKT_KEY_DELETE 0x89
#define ZKT_KEY_F1     0x90 /* F1-F10: 0x90-0x99 */

/* SYS_SEEK whence. */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

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
#define ZKT_TYPE_PIPE   3
#ifndef __ASSEMBLER__
#include <stdint.h>
struct zkt_dirent {
	char name[ZKT_NAME_MAX + 1];
	uint32_t type;
	uint32_t size;
};

/* SYS_POLL: the kernel sets `ready` when a read of `fd` would not block
 * (a file on a remote server always counts as ready). */
struct zkt_pollfd {
	int32_t fd;
	uint32_t ready;
};
#endif

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
#define EACCES       13  /* authentication failed (ZRP) */
#define EFAULT       14
#define EBUSY        16
#define EEXIST       17
#define ENODEV       19
#define ENOTDIR      20
#define EISDIR       21
#define EINVAL       22
#define EMFILE       24
#define EROFS        30
#define ERANGE       34
#define ENAMETOOLONG 36
#define ESPIPE       29  /* a seek on a pipe */
#define EPIPE        32
#define ENOSYS       38
#define EPROTO       71  /* a malformed protocol message (ZRP) */
#define EADDRINUSE   98
#define ENETUNREACH 101
#define ETIMEDOUT   110
#define EHOSTUNREACH 113

#endif
