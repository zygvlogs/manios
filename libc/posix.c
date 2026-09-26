/* POSIX calls ManiOS has no counterpart for. Each says so the way POSIX
 * allows: mmap() and ioctl() fail, signals are never delivered, and
 * files can't be created, renamed or removed (EROFS: only devices and
 * pipes take writes). */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
	(void)addr, (void)len, (void)prot, (void)flags, (void)fd, (void)offset;
	errno = ENODEV;
	return MAP_FAILED;
}

int munmap(void *addr, size_t len)
{
	(void)addr, (void)len;
	errno = EINVAL; /* nothing is ever mapped */
	return -1;
}

int madvise(void *addr, size_t len, int advice)
{
	(void)addr, (void)len, (void)advice;
	return 0;
}

int ioctl(int fd, unsigned long request, ...)
{
	(void)fd, (void)request;
	errno = ENOTTY;
	return -1;
}

static sig_t handlers[NSIG];

sig_t signal(int sig, sig_t handler)
{
	if (sig <= 0 || sig >= NSIG || sig == SIGKILL) {
		errno = EINVAL;
		return SIG_ERR;
	}
	sig_t old = handlers[sig];
	handlers[sig] = handler;
	return old;
}

int raise(int sig)
{
	if (sig <= 0 || sig >= NSIG) {
		errno = EINVAL;
		return -1;
	}
	sig_t h = handlers[sig];
	if (h == SIG_IGN) {
		return 0;
	}
	if (h != SIG_DFL) {
		h(sig);
		return 0;
	}
	if (sig == SIGCHLD || sig == SIGWINCH || sig == SIGINFO) {
		return 0; /* ignored by default */
	}
	_exit(128 + sig); /* the status a shell shows for the signal */
}

char *getenv(const char *name)
{
	(void)name;
	return NULL; /* ManiOS has no environment */
}

uid_t getuid(void) { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getgid(void) { return 0; }
gid_t getegid(void) { return 0; }

/* Renaming, removing and changing files need a writable file system;
 * a file that isn't there says so first (ENOENT). */
static int read_only(const char *path)
{
	struct stat sb;
	if (path && stat(path, &sb) < 0) {
		return -1;
	}
	errno = EROFS;
	return -1;
}

int unlink(const char *path) { return read_only(path); }
int rmdir(const char *path) { return read_only(path); }
int link(const char *from, const char *to) { (void)to; return read_only(from); }
int rename(const char *from, const char *to) { (void)to; return read_only(from); }
int remove(const char *path) { return read_only(path); }
int chown(const char *path, uid_t uid, gid_t gid) { (void)uid, (void)gid; return read_only(path); }
int fchown(int fd, uid_t uid, gid_t gid) { (void)fd, (void)uid, (void)gid; return read_only(NULL); }

int mkstemp(char *template)
{
	(void)template;
	errno = EROFS;
	return -1;
}

int sleep_ms(uint32_t ms); /* <manios.h>, whose open() and fstat() clash */

int kqueue(void)
{
	errno = ENOSYS;
	return -1;
}

int kevent(int kq, const struct kevent *changes, int nchanges, struct kevent *events,
           int nevents, const struct timespec *timeout)
{
	(void)kq, (void)changes, (void)nchanges, (void)events, (void)nevents, (void)timeout;
	errno = EBADF; /* there is never a kqueue to use */
	return -1;
}

int getpagesize(void)
{
	return 4096;
}

int fchdir(int fd)
{
	(void)fd;
	errno = ENOSYS;
	return -1;
}

unsigned int sleep(unsigned int seconds)
{
	sleep_ms(seconds * 1000);
	return 0;
}

time_t time(time_t *t)
{
	char buf[24];
	time_t now = -1;
	int fd = open("/dev/time", O_RDONLY);
	if (fd >= 0) {
		long n = read(fd, buf, sizeof(buf) - 1);
		close(fd);
		if (n > 0) {
			buf[n] = '\0';
			now = strtoll(buf, NULL, 10);
		}
	}
	if (t) {
		*t = now;
	}
	return now;
}
