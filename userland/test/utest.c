/* System call conformance test, run by the kernel's boot self-test
 * (zkt/kernel/user_selftest.c). Each check states behaviour the ABI
 * (zkt_abi.h) promises. Prints only failures and a summary; exits 0
 * only if every check passed. */
#include <errno.h>
#include <manios.h>
#include <stdlib.h>
#include <string.h>

#define KERNEL_ADDR 0xC0100000u /* inside the kernel image */
#define STACK_TOP   0xBFFFF000u /* the page above the stack is never mapped */
#define FAULT "/boot/test/fault"

static int checks, failures;

int main(void);

static void put(const char *s)
{
	write(1, s, strlen(s));
}

static void put_num(long v)
{
	char buf[12];
	int i = sizeof(buf);
	unsigned long u = v < 0 ? -(unsigned long)v : (unsigned long)v;
	buf[--i] = '\0';
	do {
		buf[--i] = (char)('0' + u % 10);
		u /= 10;
	} while (u);
	if (v < 0) {
		buf[--i] = '-';
	}
	put(buf + i);
}

static void check(int ok, const char *what, long got)
{
	checks++;
	if (!ok) {
		failures++;
		put("utest: FAIL: ");
		put(what);
		put(" (got ");
		put_num(got);
		put(", errno ");
		put_num(errno);
		put(")\n");
	}
}

/* A call that must fail with `err`. */
static void check_err(long r, int err, const char *what)
{
	check(r == -1 && errno == err, what, r);
}

/* Spawns path with one argument (if any) and waits: the child's status,
 * or -1000 - errno when spawn failed, -2000 - errno when wait did. */
static int run(const char *path, const char *arg)
{
	char *argv[] = { (char *)path, (char *)arg, 0 };
	int pid = spawn(path, argv);
	if (pid < 0) {
		return -1000 - errno;
	}
	int status;
	if (wait(pid, &status) != pid) {
		return -2000 - errno;
	}
	return status;
}

/* Digits only, and a time after 2020. */
static int strtol_ok(const char *s)
{
	long v = 0;
	for (; *s >= '0' && *s <= '9'; s++) {
		v = v * 10 + (*s - '0');
	}
	return *s == '\n' && v > 1577836800;
}

static int exists(const char *path)
{
	int fd = open(path, OREAD);
	if (fd >= 0) {
		close(fd);
	}
	return fd >= 0;
}

static void test_files(void)
{
	char buf[64];
	struct zkt_dirent d;

	int fd = open("/bin/hello", OREAD);
	check(fd == 3, "open returns the lowest free descriptor", fd);
	check(read(fd, buf, 4) == 4 && memcmp(buf, "\x7f" "ELF", 4) == 0,
	      "read returns the file's bytes", 0);
	check(fstat(fd, &d) == 0 && d.type == ZKT_TYPE_FILE && d.size > 4
	      && strcmp(d.name, "hello") == 0, "fstat describes the file", (long)d.type);
	long total = 4, n;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		total += n;
	}
	check(n == 0 && total == (long)d.size, "reads end at the file's size", total);
	check(close(fd) == 0, "close", 0);
	check_err(close(fd), EBADF, "closing a closed descriptor is EBADF");

	struct zkt_dirent entries[4];
	int found = 0;
	fd = open("/bin", OREAD);
	check(fstat(fd, &d) == 0 && d.type == ZKT_TYPE_DIR, "fstat of a directory", (long)d.type);
	while ((n = read(fd, entries, sizeof(entries))) > 0) {
		check(n % sizeof(struct zkt_dirent) == 0, "a directory reads as whole records", n);
		for (unsigned i = 0; i < n / sizeof(struct zkt_dirent); i++) {
			found |= strcmp(entries[i].name, "hello") == 0
			         && entries[i].type == ZKT_TYPE_FILE;
		}
	}
	check(n == 0 && found, "reading /bin lists hello", n);
	close(fd);
	fd = open("/bin", OREAD);
	check_err(read(fd, buf, sizeof(buf)), EINVAL,
	          "a buffer smaller than one directory record is EINVAL");
	close(fd);

	check_err(open("/bin/hello", OWRITE), EROFS, "the boot archive is read-only");
	check_err(open("/bin", OWRITE), EISDIR, "a directory cannot be opened for writing");
	check_err(open("/no/such/file", OREAD), ENOENT, "a missing file is ENOENT");
	check(write(1, buf, 0) == 0, "an empty write writes nothing", 0);

	int fds[ZKT_FD_MAX];
	int opened = 0;
	while (opened < ZKT_FD_MAX && (fds[opened] = open("/bin/hello", OREAD)) >= 0) {
		opened++;
	}
	check(opened == ZKT_FD_MAX - 3 && errno == EMFILE,
	      "ZKT_FD_MAX descriptors per process, then EMFILE", opened);
	while (opened > 0) {
		close(fds[--opened]);
	}
}

/* No pointer from user space reaches the kernel unchecked. */
static void test_pointers(void)
{
	char buf[16];
	struct zkt_dirent d;
	int fd = open("/bin/hello", OREAD);

	check_err(open((const char *)KERNEL_ADDR, OREAD), EFAULT, "a kernel path pointer is EFAULT");
	check_err(open(0, OREAD), EFAULT, "a NULL path is EFAULT");
	check_err(read(fd, (void *)KERNEL_ADDR, 4), EFAULT, "reading into the kernel is EFAULT");
	check_err(read(fd, (void *)(STACK_TOP - 8), 16), EFAULT,
	          "a buffer running off the stack is EFAULT");
	check_err(read(fd, (void *)(uintptr_t)main, 4), EFAULT, "reading into program code is EFAULT");
	check(read(fd, buf, 4) == 4 && memcmp(buf, "\x7f" "ELF", 4) == 0,
	      "a failed read consumes nothing", 0);
	check_err(write(1, (const void *)KERNEL_ADDR, 4), EFAULT, "writing from the kernel is EFAULT");
	check_err(fstat(fd, (struct zkt_dirent *)KERNEL_ADDR), EFAULT, "fstat into the kernel is EFAULT");
	check_err(fstat(fd, (struct zkt_dirent *)(STACK_TOP - 8)), EFAULT,
	          "fstat running off the stack is EFAULT");
	check_err(spawn("/bin/hello", (char *const *)KERNEL_ADDR), EFAULT, "a kernel argv is EFAULT");
	char *bad_arg[] = { "/bin/hello", (char *)KERNEL_ADDR, 0 };
	check_err(spawn("/bin/hello", bad_arg), EFAULT, "a kernel argument string is EFAULT");
	check(fstat(fd, &d) == 0, "the descriptor still works", 0);
	close(fd);

	check_err(read(ZKT_FD_MAX, buf, 1), EBADF, "reading a bad descriptor is EBADF");
	check_err(write(-1, buf, 1), EBADF, "writing a bad descriptor is EBADF");
	check_err(fstat(7, &d), EBADF, "fstat of a closed descriptor is EBADF");
	check(zkt_syscall(999, 0, 0, 0, 0, 0) == -ENOSYS, "an unknown system call is ENOSYS", 0);
}

static void test_time(void)
{
	check(getpid() > 0, "getpid", getpid());
	uint32_t t0 = uptime_ms();
	check(sleep_ms(50) == 0, "sleep", 0);
	uint32_t slept = uptime_ms() - t0;
	check(slept >= 40 && slept < 1000, "sleep(50) takes about 50 ms", (long)slept);
}

static void test_processes(void)
{
	check(run("/bin/hello", 0) == 0, "a child runs and exits 0", 0);
	int status = run(FAULT, "exit42");
	check(status == 42, "wait returns the exit code", status);

	/* The kernel ends a faulting program, reports the vector, and
	 * carries on. 14: page fault, 13: general protection, 0: divide. */
	static const struct { const char *mode; int vector; } faults[] = {
		{ "null", 14 }, { "kread", 14 }, { "kwrite", 14 }, { "wtext", 14 }, { "jump", 14 },
		{ "stack", 14 }, { "priv", 13 }, { "io", 13 }, { "int", 13 }, { "divide", 0 },
	};
	for (unsigned i = 0; i < sizeof(faults) / sizeof(faults[0]); i++) {
		status = run(FAULT, faults[i].mode);
		check(status == (ZKT_WAIT_KILLED | faults[i].vector), faults[i].mode, status);
	}

	check(run("/no/such/program", 0) == -1000 - ENOENT, "spawning a missing file is ENOENT", 0);
	check(run("/bin", 0) == -1000 - EISDIR, "spawning a directory is EISDIR", 0);
	check(run("/boot/etc/motd", 0) == -1000 - ENOEXEC, "spawning a text file is ENOEXEC", 0);
	check(run("/dev/cons", 0) == -1000 - ENOEXEC, "spawning a device is ENOEXEC", 0);

	char *many[ZKT_ARGS_MAX + 2];
	for (int i = 0; i <= ZKT_ARGS_MAX; i++) {
		many[i] = "x";
	}
	many[ZKT_ARGS_MAX + 1] = 0;
	check_err(spawn("/bin/hello", many), E2BIG, "more than ZKT_ARGS_MAX arguments is E2BIG");
	static char long_arg[ZKT_ARG_BYTES + 1];
	memset(long_arg, 'y', ZKT_ARG_BYTES);
	char *too_long[] = { "/bin/hello", long_arg, 0 };
	check_err(spawn("/bin/hello", too_long), E2BIG, "over ZKT_ARG_BYTES of arguments is E2BIG");

	check_err(wait(12345, 0), ECHILD, "waiting for a stranger is ECHILD");
	char *argv[] = { "/bin/hello", 0 };
	int pid = spawn("/bin/hello", argv);
	check(pid > 0 && pid != getpid() && wait(pid, 0) == pid, "wait with a NULL status", pid);
	check_err(wait(pid, 0), ECHILD, "a child can be waited for only once");

	/* The timer preempts user code: the parent's sleep ends on time
	 * while a child spins. */
	char *spin[] = { FAULT, "spin", 0 };
	int spinner = spawn(FAULT, spin);
	uint32_t t0 = uptime_ms();
	sleep_ms(20);
	uint32_t slept = uptime_ms() - t0;
	check(spinner > 0 && wait(spinner, &status) == spinner && status == 0 && slept < 200,
	      "a CPU-bound process does not starve others", (long)slept);

	/* Two copies at once use the same addresses. */
	char *iso[] = { "/boot/test/isotest", 0 };
	int a = spawn(iso[0], iso), b = spawn(iso[0], iso);
	int sa = -1, sb = -1;
	wait(a, &sa);
	wait(b, &sb);
	check(sa == 0 && sb == 0, "processes have separate address spaces", sa * 256 + sb);
}

/* The current directory (M9): relative paths, "..", inheritance. */
static void test_directories(void)
{
	char buf[ZKT_PATH_MAX + 1];
	check(getcwd(buf, sizeof(buf)) && !strcmp(buf, "/"), "a kernel-started program starts at /", 0);
	check(chdir("/boot/test/../etc/.") == 0 && getcwd(buf, sizeof(buf))
	      && !strcmp(buf, "/boot/etc"), "chdir cleans the path", 0);
	check(getcwd(buf, 9) == NULL && errno == ERANGE && getcwd(buf, 10) == buf,
	      "getcwd needs room for the NUL, else ERANGE", 0);
	check(exists("motd") && exists("../bin/hello") && exists("/bin/hello"),
	      "relative paths start at the current directory", 0);
	check(chdir("..") == 0 && exists("bin/hello") && chdir("../../..") == 0
	      && getcwd(buf, sizeof(buf)) && !strcmp(buf, "/"), ".. stops at the root", 0);
	check_err(chdir("/boot/etc/motd"), ENOTDIR, "chdir to a file is ENOTDIR");
	check_err(chdir("/no/such/dir"), ENOENT, "chdir to a missing directory is ENOENT");
	check(getcwd((char *)KERNEL_ADDR, 64) == NULL && errno == EFAULT, "getcwd into the kernel is EFAULT", 0);

	check(chdir("/boot") == 0 && run("test/nstest", "cwd") == 0,
	      "a child starts in its parent's directory, and spawn takes relative paths", 0);
	chdir("/");
}

/* The heap (M9). */
static void test_sbrk(void)
{
	char *start = sbrk(0);
	check(start != (void *)-1 && ((uintptr_t)start & 0xFFF) == 0 && (uintptr_t)start > (uintptr_t)main,
	      "the heap starts on a page after the program", (long)(uintptr_t)start);
	check(sbrk(3 * 4096 + 5) == start, "sbrk returns the previous end", 0);
	int zeroed = 1;
	for (int i = 0; i < 3 * 4096 + 5; i++) {
		zeroed &= start[i] == 0;
		start[i] = (char)i;
	}
	check(zeroed && start[3 * 4096 + 4] == (char)(3 * 4096 + 4), "new heap memory is zeroed and writable", 0);
	int fd = open("/bin/hello", OREAD);
	check_err(read(fd, start + 3 * 4096 + 5, 4096), EFAULT, "the heap ends at the break's page");
	close(fd);
	check(sbrk(-(3 * 4096 + 5)) == start + 3 * 4096 + 5 && sbrk(0) == start, "the heap shrinks", 0);
	check_err((long)sbrk(-1), EINVAL, "shrinking below the start is EINVAL");
	check_err((long)sbrk(INT32_MIN), EINVAL, "sbrk(INT32_MIN) is EINVAL");
	check(run(FAULT, "shrunk") == (ZKT_WAIT_KILLED | 14), "memory given back by sbrk faults", 0);
}

/* Pipes, descriptors, poll, reap (M12). */
static void test_pipes(void)
{
	int fds[2], status;
	char buf[64];
	check(pipe(fds) == 0 && fds[0] >= 3 && fds[1] > fds[0], "pipe", 0);
	check(write(fds[0], "hello", 5) == 5 && write(fds[0], "world!", 6) == 6, "writes to a pipe", 0);
	check(read(fds[1], buf, sizeof(buf)) == 5 && !memcmp(buf, "hello", 5),
	      "a read returns one message", 0);
	check(read(fds[1], buf, 3) == 3 && !memcmp(buf, "wor", 3) && read(fds[1], buf, 10) == 3
	      && !memcmp(buf, "ld!", 3), "a short read leaves the rest of the message", 0);
	check(write(fds[1], "back", 4) == 4 && read(fds[0], buf, sizeof(buf)) == 4,
	      "a pipe works in both directions", 0);
	check(write(fds[0], "", 0) == 0 && read(fds[1], buf, sizeof(buf)) == 0,
	      "an empty write reads as end of file", 0);

	struct zkt_pollfd pf[2] = { { fds[1], 1 }, { fds[0], 1 } };
	check(poll(pf, 2, 0) == 0 && !pf[0].ready && !pf[1].ready, "poll: nothing to read", 0);
	uint32_t t0 = uptime_ms();
	long waited;
	check(poll(pf, 1, 50) == 0 && (waited = (long)(uptime_ms() - t0)) >= 40 && waited < 1000,
	      "poll times out", 0);
	write(fds[0], "x", 1);
	check(poll(pf, 2, -1) == 1 && pf[0].ready && !pf[1].ready, "poll finds the readable end", 0);
	read(fds[1], buf, 1);
	check_err(poll(pf, ZKT_FD_MAX + 1, 0), EINVAL, "polling too many descriptors is EINVAL");
	struct zkt_pollfd bad = { 99, 0 };
	check_err(poll(&bad, 1, 0), EBADF, "polling a bad descriptor is EBADF");

	int d = dup(fds[0]);
	check(d > fds[1] && write(d, "via dup", 7) == 7 && read(fds[1], buf, sizeof(buf)) == 7,
	      "dup shares the file", d);
	check(dup2(fds[0], 9) == 9 && write(9, "nine", 4) == 4 && read(fds[1], buf, sizeof(buf)) == 4,
	      "dup2 to a chosen descriptor", 0);
	check_err(dup2(fds[0], ZKT_FD_MAX), EBADF, "dup2 beyond the table is EBADF");
	check_err(dup(99), EBADF, "dup of a bad descriptor is EBADF");
	close(d);
	close(9);
	close(fds[0]);
	check(poll(pf, 1, 0) == 1, "a pipe whose other end is gone polls readable", 0);
	check(read(fds[1], buf, sizeof(buf)) == 0, "the other end closed: end of file", 0);
	check_err(write(fds[1], "x", 1), EPIPE, "writing with no reader is EPIPE");
	check_err(lseek(fds[1], 0, SEEK_CUR), ESPIPE, "seeking a pipe is ESPIPE");
	/* Each program gets what its ABI version promised (zkt_abi.h). */
	int seek_status = run("/boot/test/pipeseek", 0);
	check(seek_status == 1, "a version-2 program: seeking a pipe fails", seek_status);
	seek_status = run("/boot/test/pipeseek1", 0);
	check(seek_status == 0, "a version-1 program: seeking a pipe works as before", seek_status);
	close(fds[1]);

	struct zkt_dirent st;
	pipe(fds);
	check(fstat(fds[0], &st) == 0 && st.type == ZKT_TYPE_PIPE, "fstat of a pipe", (long)st.type);
	static char big[100000], back[100000];
	for (unsigned i = 0; i < sizeof(big); i++) {
		big[i] = (char)(i * 7);
	}
	check(write(fds[0], big, sizeof(big)) == sizeof(big) && read(fds[1], back, sizeof(back)) == sizeof(back)
	      && !memcmp(big, back, sizeof(big)), "a 100 KB message arrives whole", 0);

	/* A child's standard output into a pipe: its end of file comes when
	 * the child exits, since it holds the only other copy. */
	int saved = dup(1);
	dup2(fds[0], 1);
	char *argv[] = { "/bin/hello", "piped", 0 };
	int pid = spawn(argv[0], argv);
	dup2(saved, 1);
	close(saved);
	close(fds[0]);
	char out[256];
	long n, total = 0;
	while ((n = read(fds[1], out + total, sizeof(out) - 1 - (size_t)total)) > 0) {
		total += n;
	}
	out[total] = '\0';
	check(pid > 0 && wait(pid, &status) == pid && status == 0
	      && strstr(out, "Hello from ManiOS userspace!\nargs: piped\n"), "a child writing into a pipe",
	      total);
	close(fds[1]);

	check_err(reap(&status), ECHILD, "reap with no children is ECHILD");
	char *spin[] = { FAULT, "spin", 0 };
	pid = spawn(FAULT, spin);
	check(reap(&status) == 0, "reap while the child runs is 0", 0);
	int got;
	while ((got = reap(&status)) == 0) {
		sleep_ms(20);
	}
	check(got == pid && status == 0, "reap collects the child once it exits", got);

	int fd = open("/dev/time", OREAD);
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	buf[n > 0 ? n : 0] = '\0';
	check(n > 1 && buf[n - 1] == '\n' && strtol_ok(buf), "/dev/time reads as seconds", n);
	fd = open("/dev/null", ORDWR);
	check(write(fd, "gone", 4) == 4 && read(fd, buf, sizeof(buf)) == 0, "/dev/null", 0);
	close(fd);
}

static void test_namespaces(void)
{
	check(run("/boot/test/nstest", "bind") == 0 && exists("/n/bin/hello"),
	      "a child's bind shows in its parent's namespace", 0);
	check(unbind("/n") == 0 && !exists("/n/bin/hello"), "unbind", 0);
	check(run("/boot/test/nstest", "fork-unbind") == 0 && exists("/bin/hello"),
	      "after nsfork a child's changes stay private", 0);
	check_err(bind("/boot", "/n", 7), EINVAL, "an unknown bind flag is EINVAL");
	check_err(bind("/nowhere", "/n", BIND_FLAG_REPLACE), ENOENT, "binding a missing path is ENOENT");
}

/* Reads a whole text device, `chunk` bytes a read. */
static long read_all(const char *path, char *buf, size_t size, size_t chunk)
{
	int fd = open(path, OREAD);
	if (fd < 0) {
		return -1;
	}
	size_t got = 0;
	for (;;) {
		size_t want = size - 1 - got < chunk ? size - 1 - got : chunk;
		long n = want ? read(fd, buf + got, want) : 0;
		if (n <= 0) {
			break;
		}
		got += (size_t)n;
	}
	close(fd);
	buf[got] = '\0';
	return (long)got;
}

/* The number after "NAME " at the start of a line of /dev/sysstat, or -1. */
static long stat_value(const char *text, const char *name, int field)
{
	size_t len = strlen(name);
	for (const char *line = text; *line; line = strchr(line, '\n') + 1) {
		if (!strncmp(line, name, len) && line[len] == ' ') {
			char *p = (char *)line + len;
			long v = -1;
			for (int i = 0; i <= field; i++) {
				v = (long)strtoul(p, &p, 10);
			}
			return v;
		}
		if (!strchr(line, '\n')) {
			break;
		}
	}
	return -1;
}

struct ps_line {
	long pid, ppid, cpu_ms, mem_kib;
	char state[16], name[32];
};

/* Finds process `pid` in /dev/ps: 1 if found, 0 if not, -1 if a line
 * is malformed. */
static int ps_find(long pid, struct ps_line *out)
{
	static char text[4096];
	if (read_all("/dev/ps", text, sizeof(text), 5) <= 0) {
		return -1;
	}
	int found = 0;
	for (char *line = text; *line;) {
		char *end = strchr(line, '\n');
		if (!end) {
			return -1;
		}
		*end = '\0';
		struct ps_line l;
		char *p = line;
		l.pid = (long)strtoul(p, &p, 10);
		l.ppid = (long)strtoul(p, &p, 10);
		char *state = ++p, *sp = strchr(state, ' ');
		if (*p == ' ' || !sp || sp - state >= (long)sizeof(l.state)) {
			return -1;
		}
		memcpy(l.state, state, (size_t)(sp - state));
		l.state[sp - state] = '\0';
		p = sp;
		l.cpu_ms = (long)strtoul(p, &p, 10);
		l.mem_kib = (long)strtoul(p, &p, 10);
		if (*p != ' ' || strlen(p + 1) >= sizeof(l.name)) {
			return -1;
		}
		strcpy(l.name, p + 1);
		if (l.pid == pid) {
			*out = l;
			found = 1;
		}
		line = end + 1;
	}
	return found;
}

/* /dev/sysstat and /dev/ps (M18): what top, fetch and ManiDE show. */
static void test_sysstat(void)
{
	char text[512];
	check(read_all("/dev/sysstat", text, sizeof(text), 3) > 0, "/dev/sysstat reads", 0);
	long total = stat_value(text, "memory", 0), free_kib = stat_value(text, "memory", 1);
	check(total >= 4096 && free_kib > 0 && free_kib < total, "sysstat: memory TOTAL FREE", total);
	long up = stat_value(text, "uptime", 0), idle = stat_value(text, "idle", 0);
	check(up > 0 && idle >= 0 && idle <= up, "sysstat: idle time within uptime", idle);
	check(stat_value(text, "processes", 0) >= 1, "sysstat: processes", 0);
	char *version = strstr(text, "version ");
	check(version == text && !strncmp(version + 8, MANIOS_VERSION "\n", strlen(MANIOS_VERSION) + 1),
	      "sysstat: the version comes first", 0);
	check(strstr(text, "\ncpu ") && text[strlen(text) - 1] == '\n', "sysstat: a cpu line", 0);

	struct ps_line me;
	check(ps_find(getpid(), &me) == 1 && !strcmp(me.state, "running") && !strcmp(me.name, "utest")
	      && me.mem_kib >= 64, "/dev/ps: the reader is running", me.mem_kib);

	/* A CPU-bound child: its time counts, and the CPU is busy meanwhile. */
	char *spin[] = { FAULT, "spin", 0 };
	int spinner = spawn(FAULT, spin);
	sleep_ms(150);
	read_all("/dev/sysstat", text, sizeof(text), 64);
	long up1 = stat_value(text, "uptime", 0), idle1 = stat_value(text, "idle", 0);
	struct ps_line child;
	check(ps_find(spinner, &child) == 1 && child.ppid == getpid() && !strcmp(child.name, "fault")
	      && !strcmp(child.state, "ready") && child.cpu_ms >= 50, "/dev/ps: a busy child's CPU time",
	      child.cpu_ms);
	sleep_ms(100);
	read_all("/dev/sysstat", text, sizeof(text), 64);
	long busy_up = stat_value(text, "uptime", 0) - up1, busy_idle = stat_value(text, "idle", 0) - idle1;
	check(busy_up >= 90 && busy_idle * 4 < busy_up, "sysstat: no idle time while a process spins",
	      busy_idle);

	/* Once it has finished, and until it is waited for: exited. */
	sleep_ms(300);
	check(ps_find(spinner, &child) == 1 && !strcmp(child.state, "exited") && child.mem_kib == 0
	      && child.cpu_ms >= 200, "/dev/ps: an exited child not yet waited for", child.cpu_ms);
	int status;
	check(wait(spinner, &status) == spinner && status == 0 && ps_find(spinner, &child) == 0,
	      "/dev/ps: a child waited for is gone", 0);

	read_all("/dev/sysstat", text, sizeof(text), 64);
	up1 = stat_value(text, "uptime", 0);
	idle1 = stat_value(text, "idle", 0);
	sleep_ms(100);
	read_all("/dev/sysstat", text, sizeof(text), 64);
	long quiet_up = stat_value(text, "uptime", 0) - up1, quiet_idle = stat_value(text, "idle", 0) - idle1;
	check(quiet_up >= 90 && quiet_idle * 2 > quiet_up, "sysstat: idle time while all sleep", quiet_idle);
}

int main(void)
{
	/* A private namespace: nothing below leaks into the kernel's. */
	check(nsfork() == 0, "nsfork", 0);
	test_files();
	test_pointers();
	test_time();
	test_processes();
	test_directories();
	test_sbrk();
	test_pipes();
	test_namespaces();
	test_sysstat();

	if (failures) {
		put("utest: ");
		put_num(failures);
		put(" of ");
		put_num(checks);
		put(" checks failed\n");
		return 1;
	}
	put("utest: all ");
	put_num(checks);
	put(" checks passed\n");
	return 0;
}
