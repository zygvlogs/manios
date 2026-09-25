/* The cluster self-test, run by the kernel at boot (M13). Over the
 * loopback network, through the machine's own ZRP server:
 *
 *  - a process exports its namespace (SYS_EXPORT) and mounts the export
 *    back over UDP, reaching a file server that is itself a program
 *    (cltest serve, over a pipe): three layers of the same protocol;
 *  - a read that waits there is kept alive with Rpending while other
 *    requests on the same mount are answered;
 *  - export names: taken, invalid, another process's, and gone when the
 *    process that made them exits;
 *  - /dev/sysname, /dev/net and /dev/zrp.
 *
 * The node's cluster key, if it has one, is used both ways. */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zrpsrv.h>

#define HELLO "hello over the network\n"

/* --- the file server: hello, wait (answered when release is written) --- */

static long serve_read(struct zsrv *s, struct zsrv_req *req, void *buf)
{
	(void)s;
	const char *name = req->fid->node->name;
	if (!strcmp(name, "wait")) {
		return ZSRV_DEFER;
	}
	uint32_t len = sizeof(HELLO) - 1;
	if (strcmp(name, "hello") || req->offset >= len) {
		return 0;
	}
	uint32_t n = len - req->offset < req->count ? len - req->offset : req->count;
	memcpy(buf, HELLO + req->offset, n);
	return n;
}

static long serve_write(struct zsrv *s, struct zsrv_fid *f, uint32_t offset, const void *buf,
                        uint32_t count)
{
	(void)offset;
	(void)buf;
	if (strcmp(f->node->name, "release")) {
		return -EROFS;
	}
	while (s->deferred) {
		zsrv_respond(s, s->deferred, "released\n", 9);
	}
	return count;
}

static int serve(void)
{
	static const struct zsrv_ops ops = { serve_read, serve_write, NULL };
	struct zsrv *s = zsrv_new(0, &ops, NULL);
	if (!s || !zsrv_add(&s->root, "hello", ZKT_TYPE_FILE, NULL)
	    || !zsrv_add(&s->root, "wait", ZKT_TYPE_FILE, NULL)
	    || !zsrv_add(&s->root, "release", ZKT_TYPE_FILE, NULL)) {
		return 2;
	}
	while (zsrv_handle(s) == 0) {
	}
	int left = zsrv_fid_count(s);
	zsrv_free(s);
	return left ? 3 : 0;
}

/* --- the test --- */

static int checks, failures;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failures++;
		printf("cltest: FAIL: %s (errno %d)\n", what, errno);
	}
}

static void check_err(long r, int err, const char *what)
{
	check(r == -1 && errno == err, what);
}

static long slurp(const char *path, char *buf, long size)
{
	int fd = open(path, OREAD);
	if (fd < 0) {
		return -1;
	}
	long total = 0, n;
	while (total < size - 1 && (n = read(fd, buf + total, size - 1 - total)) > 0) {
		total += n;
	}
	close(fd);
	buf[total] = '\0';
	return total;
}

static int run(const char *mode, int *pid_out)
{
	char *argv[] = { "/boot/test/cltest", (char *)mode, NULL };
	int pid = spawn(argv[0], argv);
	if (pid_out) {
		*pid_out = pid;
		return pid;
	}
	int status = -1;
	if (pid < 0 || wait(pid, &status) != pid) {
		return -1;
	}
	return status;
}

/* The value after `word ` in /dev/zrp. */
static long zrp_stat(const char *word)
{
	char text[1024];
	if (slurp("/dev/zrp", text, sizeof(text)) <= 0) {
		return -1;
	}
	size_t n = strlen(word);
	for (char *p = text; (p = strstr(p, word)); p += n) {
		if ((p == text || p[-1] == ' ' || p[-1] == '\n') && p[n] == ' ') {
			return strtol(p + n + 1, NULL, 10);
		}
	}
	return -1;
}

static void test_devices(void)
{
	char buf[512];
	check(slurp("/dev/sysname", buf, sizeof(buf)) > 1 && buf[strlen(buf) - 1] == '\n',
	      "/dev/sysname names this machine");
	check(slurp("/dev/net", buf, sizeof(buf)) > 0 && strstr(buf, "lo 127.0.0.1/8 gateway 0.0.0.0\n"),
	      "/dev/net lists the loopback interface");
	check(slurp("/dev/zrp", buf, sizeof(buf)) > 0
	      && (!strncmp(buf, "key set\n", 8) || !strncmp(buf, "key none\n", 9)),
	      "/dev/zrp says whether there is a cluster key, and not what it is");
}

static void test_names(void)
{
	check(export("/n", "cltest") == 0, "export");
	check_err(export("/n", "cltest"), EEXIST, "an export name used twice");
	check_err(export("/n/hello", "file"), ENOTDIR, "exporting a file");
	check_err(export("/n", ""), EINVAL, "the default export is the kernel's");
	check_err(export("/n", "bad/name"), EINVAL, "a name with a slash");
	check_err(unexport("nothing"), ENOENT, "taking back a name that isn't exported");
	check(run("other", 0) == 0, "another process can't take our export back");
	check(run("exporter", 0) == 0 && mount("udp!127.0.0.1", "/mnt/term", BIND_FLAG_REPLACE, "gone") == -1
	      && errno == ENOENT, "an export goes when the process that made it exits");
}

static void test_network(void)
{
	char buf[64];
	check(mount("udp!127.0.0.1", "/mnt/term", BIND_FLAG_REPLACE, "cltest") == 0,
	      "mounting our own export over the network");
	check(slurp("/mnt/term/hello", buf, sizeof(buf)) == sizeof(HELLO) - 1 && !strcmp(buf, HELLO),
	      "a file served by a program, through an export, over UDP");

	long pending = zrp_stat("pending");
	int waiter;
	run("waiter", &waiter);
	check(waiter > 0, "starting the waiter");
	/* Its read waits in the program; the client asks again, and the
	 * server answers Rpending. */
	sleep_ms(1500);
	int status;
	check(reap(&status) == 0, "the waiter's read waits");
	check(zrp_stat("pending") > pending, "a repeat of a waiting read is answered Rpending");
	check(slurp("/mnt/term/hello", buf, sizeof(buf)) == sizeof(HELLO) - 1,
	      "another request on the same mount is answered meanwhile");
	int fd = open("/n/release", OWRITE);
	check(fd >= 0 && write(fd, "go", 2) == 2, "releasing the read");
	close(fd);
	check(waiter > 0 && wait(waiter, &status) == waiter && status == 0,
	      "the waiting read gets its answer over the network");

	check(unexport("cltest") == 0, "taking the export back");
	check(slurp("/mnt/term/hello", buf, sizeof(buf)) == sizeof(HELLO) - 1,
	      "a mount made before keeps working");
	check_err(mount("udp!127.0.0.1", "/n", BIND_FLAG_AFTER, "cltest"), ENOENT,
	          "a taken-back export can't be mounted");
	check(unbind("/mnt/term") == 0, "unmounting");
}

int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "";
	if (!strcmp(mode, "serve")) {
		return serve();
	}
	if (!strcmp(mode, "other")) {
		return unexport("cltest") == -1 && errno == EPERM ? 0 : 1;
	}
	if (!strcmp(mode, "exporter")) {
		return export("/", "gone") == 0 ? 0 : 1;
	}
	if (!strcmp(mode, "waiter")) {
		char buf[16];
		int fd = open("/mnt/term/wait", OREAD);
		long n = fd < 0 ? -1 : read(fd, buf, sizeof(buf));
		close(fd);
		return n == 9 && !memcmp(buf, "released\n", 9) ? 0 : 1;
	}

	/* A namespace of our own, with the file server at /n. */
	int fds[2], saved = dup(0);
	check(nsfork() == 0 && pipe(fds) == 0, "setting up");
	dup2(fds[1], 0);
	int server;
	run("serve", &server);
	dup2(saved, 0);
	close(saved);
	close(fds[1]);
	check(server > 0 && mountfd(fds[0], "/n", BIND_FLAG_REPLACE, NULL) == 0, "mounting the server");
	close(fds[0]);

	test_devices();
	test_names();
	test_network();

	check(unbind("/n") == 0, "unmounting the server");
	int status = -1;
	check(server > 0 && wait(server, &status) == server && status == 0,
	      "the server exits cleanly, with every fid clunked");
	if (failures) {
		printf("cltest: %d of %d checks failed\n", failures, checks);
		return 1;
	}
	printf("cltest: all %d checks passed\n", checks);
	return 0;
}
