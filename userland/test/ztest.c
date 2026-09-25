/* The userspace file server test, run by the kernel's boot self-test
 * (M12). `ztest` starts `ztest serve` with one end of a pipe as its
 * standard input, mounts the other end on /n with mountfd(), and uses the
 * files the server makes up through the ordinary system calls:
 *
 *   hello      a fixed text          pending   how many reads are deferred
 *   dir/inner  a file in a directory counter   write numbers, read the sum
 *   wait       a read deferred until big       40000 bytes, several messages
 *   release    is written            remove    writing removes dir
 *
 * `ztest waiter` blocks reading /n/wait while its parent carries on,
 * which only works because a channel carries many requests at once.
 * When the parent unmounts /n the server sees end of file, and exits 0
 * only if the kernel clunked every fid it had opened. */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zrpsrv.h>

#define HELLO "hello from userspace\n"
#define BIG 40000

static uint8_t big_byte(uint32_t i)
{
	return (uint8_t)(i * 7 + i / 251);
}

/* --- the server --- */

enum file { F_HELLO = 1, F_INNER, F_WAIT, F_RELEASE, F_PENDING, F_COUNTER, F_BIG, F_REMOVE };

static long total;
static struct zsrv_node *dir_node;

static long text(struct zsrv_req *req, void *buf, const char *s)
{
	uint32_t len = (uint32_t)strlen(s);
	if (req->offset >= len) {
		return 0;
	}
	uint32_t n = len - req->offset < req->count ? len - req->offset : req->count;
	memcpy(buf, s + req->offset, n);
	return n;
}

static long serve_read(struct zsrv *s, struct zsrv_req *req, void *buf)
{
	char num[16];
	switch ((enum file)(intptr_t)req->fid->node->aux) {
	case F_HELLO:
		return text(req, buf, HELLO);
	case F_INNER:
		return text(req, buf, "inner\n");
	case F_WAIT:
		return ZSRV_DEFER;
	case F_PENDING: {
		int n = 0;
		for (struct zsrv_req *r = s->deferred; r; r = r->next) {
			n++;
		}
		snprintf(num, sizeof(num), "%d\n", n);
		return text(req, buf, num);
	}
	case F_COUNTER:
		snprintf(num, sizeof(num), "%ld\n", total);
		return text(req, buf, num);
	case F_BIG: {
		uint32_t n = 0;
		for (uint32_t i = req->offset; i < BIG && n < req->count; i++, n++) {
			((uint8_t *)buf)[n] = big_byte(i);
		}
		return n;
	}
	default:
		return 0;
	}
}

static long serve_write(struct zsrv *s, struct zsrv_fid *f, uint32_t offset, const void *buf,
                        uint32_t count)
{
	(void)offset;
	char num[16];
	switch ((enum file)(intptr_t)f->node->aux) {
	case F_RELEASE:
		while (s->deferred) {
			zsrv_respond(s, s->deferred, "released\n", 9);
		}
		return count;
	case F_COUNTER:
		if (count >= sizeof(num)) {
			return -EINVAL;
		}
		memcpy(num, buf, count);
		num[count] = '\0';
		total += strtol(num, NULL, 10);
		return count;
	case F_REMOVE:
		if (dir_node) {
			zsrv_remove(s, dir_node);
			dir_node = NULL;
		}
		return count;
	default:
		return -EROFS;
	}
}

static const struct zsrv_ops serve_ops = { serve_read, serve_write, NULL };

static int serve(void)
{
	struct zsrv *s = zsrv_new(0, &serve_ops, NULL);
	if (!s) {
		return 2;
	}
	struct zsrv_node *root = &s->root;
	zsrv_add(root, "hello", ZKT_TYPE_FILE, (void *)F_HELLO)->length = sizeof(HELLO) - 1;
	dir_node = zsrv_add(root, "dir", ZKT_TYPE_DIR, NULL);
	zsrv_add(dir_node, "inner", ZKT_TYPE_FILE, (void *)F_INNER);
	zsrv_add(root, "wait", ZKT_TYPE_FILE, (void *)F_WAIT);
	zsrv_add(root, "release", ZKT_TYPE_FILE, (void *)F_RELEASE);
	zsrv_add(root, "pending", ZKT_TYPE_FILE, (void *)F_PENDING);
	zsrv_add(root, "counter", ZKT_TYPE_FILE, (void *)F_COUNTER);
	zsrv_add(root, "big", ZKT_TYPE_FILE, (void *)F_BIG)->length = BIG;
	zsrv_add(root, "remove", ZKT_TYPE_FILE, (void *)F_REMOVE);
	while (zsrv_handle(s) == 0) {
	}
	int left = zsrv_fid_count(s);
	if (left) {
		printf("ztest: serve: %d fids left open at end of file\n", left);
	}
	zsrv_free(s);
	return left ? 3 : 0;
}

/* --- the client --- */

static int checks, failures;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failures++;
		printf("ztest: FAIL: %s (errno %d)\n", what, errno);
	}
}

static void check_err(long r, int err, const char *what)
{
	check(r == -1 && errno == err, what);
}

/* The whole file, NUL-terminated, in buf; its length or -1. */
static long slurp(const char *path, char *buf, long size)
{
	int fd = open(path, OREAD);
	if (fd < 0) {
		return -1;
	}
	long total_read = 0, n;
	while (total_read < size - 1 && (n = read(fd, buf + total_read, size - 1 - total_read)) > 0) {
		total_read += n;
	}
	close(fd);
	buf[total_read] = '\0';
	return total_read;
}

static long put_file(const char *path, const char *s)
{
	int fd = open(path, OWRITE);
	if (fd < 0) {
		return -1;
	}
	long n = write(fd, s, strlen(s));
	close(fd);
	return n;
}

static int start(const char *mode)
{
	char *argv[] = { "/boot/test/ztest", (char *)mode, NULL };
	return spawn(argv[0], argv);
}

static void test_tree(void)
{
	static const char *const names[] = { "hello", "dir", "wait", "release", "pending",
		                                  "counter", "big", "remove" };
	struct zkt_dirent e, st;
	int fd = open("/n", OREAD), n = 0, ordered = 1;
	while (fd >= 0 && read(fd, &e, sizeof(e)) == sizeof(e)) {
		ordered &= n < 8 && !strcmp(e.name, names[n]);
		n++;
	}
	close(fd);
	check(fd >= 0 && n == 8 && ordered, "the server's directory lists its files in order");

	char buf[64];
	check(slurp("/n/hello", buf, sizeof(buf)) == sizeof(HELLO) - 1 && !strcmp(buf, HELLO),
	      "reading a file the program makes up");
	check(slurp("/n/dir/inner", buf, sizeof(buf)) == 6 && !strcmp(buf, "inner\n"),
	      "walking into a subdirectory");
	fd = open("/n/hello", OREAD);
	check(fd >= 0 && fstat(fd, &st) == 0 && st.type == ZKT_TYPE_FILE && st.size == 21,
	      "fstat reports the node's type and length");
	check(lseek(fd, 6, SEEK_SET) == 6 && read(fd, buf, 4) == 4 && !memcmp(buf, "from", 4),
	      "reading at an offset");
	close(fd);
	fd = open("/n/dir", OREAD);
	check(fd >= 0 && fstat(fd, &st) == 0 && st.type == ZKT_TYPE_DIR, "a directory is a directory");
	close(fd);

	check_err(open("/n/missing", OREAD), ENOENT, "a missing name is ENOENT");
	check_err(open("/n/hello/x", OREAD), ENOTDIR, "walking through a file is ENOTDIR");
	fd = open("/n/hello", OWRITE);
	check_err(fd < 0 ? fd : write(fd, "x", 1), EROFS, "a file without a writer is EROFS");
	close(fd);
}

static void test_data(void)
{
	check(put_file("/n/counter", "5") == 1 && put_file("/n/counter", "37") == 2,
	      "writing to the server");
	char buf[16];
	check(slurp("/n/counter", buf, sizeof(buf)) == 3 && !strcmp(buf, "42\n"),
	      "the server saw the writes");
	check_err(put_file("/n/counter", "12345678901234567890"), EINVAL,
	          "the server's own error comes back as errno");

	/* More than one message: the kernel splits reads at msize. */
	uint8_t *big = malloc(BIG + 16);
	long got = big ? slurp("/n/big", (char *)big, BIG + 16) : -1;
	int same = got == BIG;
	for (uint32_t i = 0; same && i < BIG; i++) {
		same = big[i] == big_byte(i);
	}
	check(same, "a 40000-byte file arrives intact across several messages");
	free(big);
}

static void test_deferred(void)
{
	char buf[32];
	int waiter = start("waiter");
	check(waiter > 0, "starting the waiter");
	/* Until its read is in the server's deferred list. */
	uint32_t deadline = uptime_ms() + 3000;
	while (slurp("/n/pending", buf, sizeof(buf)) >= 0 && strcmp(buf, "1\n")
	       && uptime_ms() < deadline) {
		sleep_ms(10);
	}
	check(!strcmp(buf, "1\n"), "the waiter's read is deferred");
	int status;
	check(slurp("/n/hello", buf, sizeof(buf)) == 21 && reap(&status) == 0,
	      "other requests are answered while a read waits");
	check(put_file("/n/release", "go") == 2, "releasing the deferred read");
	check(waiter > 0 && wait(waiter, &status) == waiter && status == 0,
	      "the deferred read got its answer");
	check(slurp("/n/pending", buf, sizeof(buf)) == 2 && !strcmp(buf, "0\n"),
	      "nothing is left deferred");

	/* Removing a node the client has open: it fails from then on. */
	int fd = open("/n/dir/inner", OREAD);
	check(fd >= 0 && put_file("/n/remove", "x") == 1, "removing a directory");
	check_err(read(fd, buf, sizeof(buf)), EIO, "a removed file reads as EIO");
	close(fd);
	check_err(open("/n/dir", OREAD), ENOENT, "a removed directory is gone");
}

static int waiter_main(void)
{
	/* One read: a second would wait for another release. */
	char buf[32];
	int fd = open("/n/wait", OREAD);
	long n = fd < 0 ? -1 : read(fd, buf, sizeof(buf));
	close(fd);
	return n == 9 && !memcmp(buf, "released\n", 9) ? 0 : 1;
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "serve")) {
		return serve();
	}
	if (argc > 1 && !strcmp(argv[1], "waiter")) {
		return waiter_main();
	}

	/* Our own namespace: /n is ours to mount on. */
	if (nsfork() < 0) {
		return 1;
	}
	int fds[2], saved = dup(0);
	check(pipe(fds) == 0, "pipe");
	dup2(fds[1], 0);
	int server = start("serve");
	dup2(saved, 0);
	close(saved);
	close(fds[1]);
	check(server > 0, "starting the server");

	int null = open("/dev/null", ORDWR);
	check_err(mountfd(null, "/n", BIND_FLAG_REPLACE, ""), EINVAL, "mountfd needs a pipe");
	close(null);
	check_err(mountfd(99, "/n", BIND_FLAG_REPLACE, ""), EBADF, "mountfd on a bad descriptor");
	check_err(mountfd(fds[0], "/n", BIND_FLAG_REPLACE, "other"), ENOENT,
	          "the server refuses an export it doesn't have");
	check(mountfd(fds[0], "/n", BIND_FLAG_REPLACE, "") == 0, "mountfd");
	close(fds[0]); /* the mount keeps the channel */

	test_tree();
	test_data();
	test_deferred();

	/* The last reference to the session goes: the server reads end of file. */
	check(unbind("/n") == 0, "unmounting");
	int status = -1;
	check(server > 0 && wait(server, &status) == server && status == 0,
	      "the server exits cleanly, with every fid clunked");

	if (failures) {
		printf("ztest: %d of %d checks failed\n", failures, checks);
		return 1;
	}
	printf("ztest: all %d checks passed\n", checks);
	return 0;
}
