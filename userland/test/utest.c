/* System call conformance test, run by the kernel's boot self-test
 * (zkt/kernel/user_selftest.c). Each check states behaviour the ABI
 * (zkt_abi.h) promises. Prints only failures and a summary; exits 0
 * only if every check passed. */
#include <errno.h>
#include <manios.h>
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
	check_err(open("bin/hello", OREAD), EINVAL, "a relative path is EINVAL");
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

int main(void)
{
	/* A private namespace: nothing below leaks into the kernel's. */
	check(nsfork() == 0, "nsfork", 0);
	test_files();
	test_pointers();
	test_time();
	test_processes();
	test_namespaces();

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
