#include <errno.h>
#include <manios.h>

int errno;

long zkt_syscall(uint32_t num, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                 uint32_t a4)
{
	long r;
	__asm__ volatile("int $0x80"
	                 : "=a"(r)
	                 : "a"(num), "b"(a0), "c"(a1), "d"(a2), "S"(a3), "D"(a4)
	                 : "memory");
	return r;
}

/* Only -1..-ZKT_ERRNO_MAX mean failure: a result can be an address
 * above 2 GiB, which is negative as a long. */
static long check(long r)
{
	if ((unsigned long)r >= (unsigned long)-ZKT_ERRNO_MAX) {
		errno = (int)-r;
		return -1;
	}
	return r;
}

#define SC0(n)          check(zkt_syscall((n), 0, 0, 0, 0, 0))
#define SC1(n, a)       check(zkt_syscall((n), (uint32_t)(a), 0, 0, 0, 0))
#define SC2(n, a, b)    check(zkt_syscall((n), (uint32_t)(a), (uint32_t)(b), 0, 0, 0))
#define SC3(n, a, b, c) check(zkt_syscall((n), (uint32_t)(a), (uint32_t)(b), (uint32_t)(c), 0, 0))

__attribute__((noreturn)) void _exit(int code)
{
	zkt_syscall(SYS_EXIT, (uint32_t)code, 0, 0, 0, 0);
	for (;;) {
	}
}

long read(int fd, void *buf, size_t len)        { return SC3(SYS_READ, fd, buf, len); }
long write(int fd, const void *buf, size_t len) { return SC3(SYS_WRITE, fd, buf, len); }
int open(const char *path, int mode)            { return (int)SC2(SYS_OPEN, path, mode); }
long lseek(int fd, long offset, int whence)     { return SC3(SYS_SEEK, fd, offset, whence); }
int close(int fd)                               { return (int)SC1(SYS_CLOSE, fd); }
int fstat(int fd, struct zkt_dirent *out)       { return (int)SC2(SYS_FSTAT, fd, out); }
int spawn(const char *path, char *const argv[]) { return (int)SC2(SYS_SPAWN, path, argv); }
int wait(int pid, int *status)                  { return (int)SC2(SYS_WAIT, pid, status); }
int getpid(void)                                { return (int)SC0(SYS_GETPID); }
int sleep_ms(uint32_t ms)                       { return (int)SC1(SYS_SLEEP, ms); }
uint32_t uptime_ms(void)                        { return (uint32_t)zkt_syscall(SYS_UPTIME, 0, 0, 0, 0, 0); }
int bind(const char *new_path, const char *old_path, int flag)
{
	return (int)SC3(SYS_BIND, new_path, old_path, flag);
}
int unbind(const char *old_path)                { return (int)SC1(SYS_UNBIND, old_path); }
int nsfork(void)                                { return (int)SC0(SYS_NSFORK); }
int chdir(const char *path)                     { return (int)SC1(SYS_CHDIR, path); }

int mount(const char *dial, const char *old_path, int flag, const char *aname)
{
	return (int)check(zkt_syscall(SYS_MOUNT, (uint32_t)dial, (uint32_t)old_path, (uint32_t)flag,
	                              (uint32_t)aname, 0));
}

void *sbrk(intptr_t increment)
{
	long r = SC1(SYS_SBRK, increment);
	return r == -1 ? (void *)-1 : (void *)r;
}

char *getcwd(char *buf, size_t size)
{
	return SC2(SYS_GETCWD, buf, size) == -1 ? NULL : buf;
}
