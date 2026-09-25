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

#define TEST_PORT 5641 /* not ZRP_PORT: the main server has that one */
#define TEST_DIAL "udp!127.0.0.1!5641"
#define KEYED_DIAL "udp!127.0.0.1!5642"

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

/* Named exports (M13): names, owners, taking them back. */
static void exports_test(struct zrp_server *srv)
{
	static const char owner;
	struct vnode *root;
	char label[40];
	check(zrp_export(srv, "etc", "/boot/etc", &owner) == 0, "selftest: net: a named export failed");
	check(zrp_export(srv, "etc", "/boot", &owner) == -EEXIST,
	      "selftest: net: an export name used twice is not EEXIST");
	check(zrp_export(srv, "bad/name", "/boot", &owner) == -EINVAL,
	      "selftest: net: a bad export name is not EINVAL");
	check(zrp_export(srv, "file", "/boot/etc/motd", &owner) == -ENOTDIR,
	      "selftest: net: exporting a file is not ENOTDIR");
	int rc = zrp_mount_key(TEST_DIAL, "etc", 0, &root, label, sizeof(label));
	check(rc == 0, "selftest: net: mounting a named export failed");
	if (rc == 0) {
		struct vnode *motd;
		check(root->ops->walk(root, "motd", &motd) == 0, "selftest: net: walking a named export");
		vnode_unref(motd);
	}
	check(zrp_unexport(srv, "etc", 0) == -EPERM,
	      "selftest: net: someone else took an export back");
	check(zrp_unexport(srv, "etc", &owner) == 0, "selftest: net: taking an export back failed");
	check(zrp_unexport(srv, "etc", &owner) == -ENOENT,
	      "selftest: net: taking back a missing export is not ENOENT");
	if (rc == 0) {
		/* Fids on it keep working after it goes. */
		struct vnode *motd;
		check(root->ops->walk(root, "motd", &motd) == 0,
		      "selftest: net: a fid on a taken-back export stopped working");
		vnode_unref(motd);
		vnode_unref(root);
	}
	check(zrp_mount_key(TEST_DIAL, "etc", 0, &root, label, sizeof(label)) == -ENOENT,
	      "selftest: net: a taken-back export can still be attached");
	check(zrp_export(srv, "mine", "/boot", &owner) == 0 && zrp_export(srv, "also", "/boot", &owner) == 0,
	      "selftest: net: exports for an owner");
	zrp_unexport_owner(srv, &owner);
	check(zrp_mount_key(TEST_DIAL, "mine", 0, &root, label, sizeof(label)) == -ENOENT
	      && zrp_mount_key(TEST_DIAL, "also", 0, &root, label, sizeof(label)) == -ENOENT,
	      "selftest: net: an owner's exports outlived it");
}

/* Authentication (M13): a server with a key admits only clients that
 * prove they know it, and a client with a key only servers that do. */
static void auth_test(void)
{
	uint8_t k1[ZRP_KEY], k2[ZRP_KEY];
	zrp_key_derive("selftest one", k1);
	zrp_key_derive("selftest two", k2);
	int err;
	struct zrp_server *keyed = zrp_serve(TEST_PORT + 1, k1, &err);
	if (!keyed || zrp_export(keyed, "", "/boot/etc", 0) != 0) {
		failure = "selftest: net: cannot start a server with a key";
		if (keyed) {
			zrp_server_stop(keyed);
		}
		return;
	}
	struct vnode *root;
	char label[40];
	int rc = zrp_mount_key(KEYED_DIAL, "", k1, &root, label, sizeof(label));
	check(rc == 0, "selftest: net: the right key was refused");
	if (rc == 0) {
		struct vnode *motd;
		check(root->ops->walk(root, "motd", &motd) == 0, "selftest: net: walking after authenticating");
		vnode_unref(motd);
		vnode_unref(root);
	}
	check(zrp_mount_key(KEYED_DIAL, "", k2, &root, label, sizeof(label)) == -EACCES,
	      "selftest: net: a wrong key is not EACCES");
	check(zrp_mount_key(KEYED_DIAL, "", 0, &root, label, sizeof(label)) == -EACCES,
	      "selftest: net: no key is not EACCES");
	check(zrp_mount_key(TEST_DIAL, "", k1, &root, label, sizeof(label)) == -EACCES,
	      "selftest: net: a server that can't prove the key was trusted");
	zrp_server_stop(keyed);
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
	struct zrp_server *srv = zrp_serve(TEST_PORT, 0, &err);
	if (!srv || zrp_export(srv, "", "/boot", 0) != 0) {
		failure = "selftest: net: cannot start a ZRP server";
		if (srv) {
			zrp_server_stop(srv);
		}
		return;
	}
	struct vnode *root;
	char label[40];
	int rc = zrp_mount_key(TEST_DIAL, "", 0, &root, label, sizeof(label));
	check(rc == 0, "selftest: net: mounting over loopback failed");
	if (rc == 0) {
		check(vfs_mount(root, label, "/n", BIND_REPLACE) == 0, "selftest: net: binding the mount");
		vnode_unref(root);

		check(same_file("/n/etc/motd", "/boot/etc/motd"), "selftest: net: a small file differs");
		check(same_file("/n/bin/sh", "/boot/bin/sh"), "selftest: net: a multi-message file differs");
		check(same_listing("/n/bin", "/boot/bin"), "selftest: net: a directory listing differs");
		check(resolve("/n/no/such") == -ENOENT, "selftest: net: a missing name is not ENOENT");
		check(zrp_mount_key("udp!127.0.0.1!5641", "other", 0, &root, label, sizeof(label)) == -ENOENT,
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
	exports_test(srv);
	auth_test();
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
