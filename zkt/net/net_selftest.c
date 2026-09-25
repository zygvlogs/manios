#include "net_selftest.h"
#include <stdbool.h>
#include "heap.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "namespace.h"
#include "net.h"
#include "panic.h"
#include "process.h"
#include "sched.h"
#include "timer.h"
#include "zkt_abi.h"
#include "zrp.h"

#define TEST_PORT 5641 /* not ZRP_PORT: an export= server may want that one */
#define TEST_DIAL "udp!127.0.0.1!5641"

static const char *failure;

static void check(bool ok, const char *what)
{
	if (!ok && !failure) {
		failure = what;
	}
}

/* Size and FNV-1a hash of a file, read in odd-sized pieces so reads
 * straddle message boundaries; false if it can't be read. */
static bool digest(const char *path, uint32_t *size, uint32_t *hash)
{
	struct file *f;
	if (vfs_open(path, OREAD, &f) != 0) {
		return false;
	}
	uint8_t buf[700];
	long n;
	*size = 0;
	*hash = 2166136261u;
	while ((n = vfs_read(f, buf, sizeof(buf))) > 0) {
		for (long i = 0; i < n; i++) {
			*hash = (*hash ^ buf[i]) * 16777619u;
		}
		*size += (uint32_t)n;
	}
	vfs_close(f);
	return n == 0;
}

static bool same_file(const char *a, const char *b)
{
	uint32_t size_a, hash_a, size_b, hash_b;
	return digest(a, &size_a, &hash_a) && digest(b, &size_b, &hash_b) && size_a == size_b
	       && hash_a == hash_b && size_a > 0;
}

/* Both directories list the same names in the same order. */
static bool same_listing(const char *a, const char *b)
{
	struct file *fa, *fb;
	if (vfs_open(a, OREAD, &fa) != 0) {
		return false;
	}
	if (vfs_open(b, OREAD, &fb) != 0) {
		vfs_close(fa);
		return false;
	}
	bool same = true;
	int entries = 0;
	for (;;) {
		struct dirent da, db;
		int ra = vfs_readdir(fa, &da), rb = vfs_readdir(fb, &db);
		if (ra != rb || ra < 0) {
			same = false;
			break;
		}
		if (ra == 0) {
			break;
		}
		same &= !strcmp(da.name, db.name) && da.type == db.type && da.size == db.size;
		entries++;
	}
	vfs_close(fa);
	vfs_close(fb);
	return same && entries > 0;
}

static int resolve(const char *path)
{
	struct file *f;
	int rc = vfs_open(path, OREAD, &f);
	if (rc == 0) {
		vfs_close(f);
	}
	return rc;
}

static void loopback_test(void *unused)
{
	(void)unused;
	struct namespace *mine = ns_fork(thread_namespace());
	if (!mine) {
		failure = "selftest: net: ns_fork failed";
		return;
	}
	thread_set_namespace(mine);
	ns_unref(mine);

	check(icmp_ping(IP_LOOPBACK, 1, 1000) >= 0, "selftest: net: ping 127.0.0.1 got no reply");

	int err;
	struct zrp_server *srv = zrp_serve("/boot", TEST_PORT, &err);
	if (!srv) {
		failure = "selftest: net: cannot start a ZRP server";
		return;
	}
	struct vnode *root;
	char label[40];
	int rc = zrp_mount(TEST_DIAL, "", &root, label, sizeof(label));
	check(rc == 0, "selftest: net: mounting over loopback failed");
	if (rc == 0) {
		check(vfs_mount(root, label, "/n", BIND_REPLACE) == 0, "selftest: net: binding the mount");
		vnode_unref(root);

		check(same_file("/n/etc/motd", "/boot/etc/motd"), "selftest: net: a small file differs");
		check(same_file("/n/bin/sh", "/boot/bin/sh"), "selftest: net: a multi-message file differs");
		check(same_listing("/n/bin", "/boot/bin"), "selftest: net: a directory listing differs");
		check(resolve("/n/no/such") == -ENOENT, "selftest: net: a missing name is not ENOENT");
		check(zrp_mount("udp!127.0.0.1!5641", "other", &root, label, sizeof(label)) == -ENOENT,
		      "selftest: net: an unknown export name is not ENOENT");

		struct file *f;
		rc = vfs_open("/n/etc/motd", OWRITE, &f);
		check(rc == 0 && vfs_write(f, "x", 1) == -EROFS,
		      "selftest: net: a write to a read-only export is not EROFS");
		if (rc == 0) {
			vfs_close(f);
		}

		/* A program loaded over the network, from its namespace. */
		char *argv[] = { "/n/bin/true", 0 };
		int status = -1, pid = process_spawn(argv[0], 1, argv, 0);
		check(pid > 0 && process_wait(0, pid, &status) == pid && status == 0,
		      "selftest: net: a program loaded over ZRP did not run");

		check(vfs_unbind("/n") == 0, "selftest: net: unbinding the mount");
		struct zrp_server_stats st;
		zrp_server_stats(srv, &st);
		check(st.fids == 0, "selftest: net: fids were not clunked when the mount went away");
	}
	zrp_server_stop(srv);
}

void net_selftest(void)
{
	size_t heap_before = heap_used();
	size_t baseline = sched_thread_count();
	failure = 0;
	if (!thread_create("net-test", loopback_test, 0)) {
		panic("selftest: net: cannot start the test thread");
	}
	uint64_t deadline = timer_uptime_ms() + 10000;
	while (sched_thread_count() > baseline) {
		if (timer_uptime_ms() > deadline) {
			panic("selftest: net: the test did not finish within 10 s");
		}
		thread_yield();
	}
	if (failure) {
		panic(failure);
	}
	if (heap_used() != heap_before || heap_check() != 0) {
		kprintf("selftest: net: heap %lu bytes before, %lu after\n", heap_before, heap_used());
		panic("selftest: net: the test leaked heap memory");
	}
}
