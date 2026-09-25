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

static long check(long r)
{
	if (r < 0) {
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
