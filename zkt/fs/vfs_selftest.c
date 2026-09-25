#include "vfs_selftest.h"
#include <stdbool.h>
#include "heap.h"
#include "kerrno.h"
#include "zkt_abi.h"
#include "namespace.h"
#include "panic.h"
#include "ramfs.h"
#include "sched.h"
#include "timer.h"

static struct vnode *tree;
static const char *failure;

static int resolve(const char *path)
{
	struct file *f;
	int rc = vfs_open(path, OREAD, &f);
	if (rc == 0) {
		vfs_close(f);
	}
	return rc;
}

static unsigned count_entries(const char *path)
{
	struct file *f;
	struct dirent d;
	unsigned n = 0;
	if (vfs_open(path, OREAD, &f) == 0) {
		while (vfs_readdir(f, &d) == 1) {
			n++;
		}
		vfs_close(f);
	}
	return n;
}

static void check(bool ok, const char *what)
{
	if (!ok && !failure) {
		failure = what;
	}
}

/* Tree: a/{x/from_a, only_a}, b/{x/from_b, only_b}. Both have an "x",
 * so which one a union finds shows the search order. */
static void private_namespace_test(void *unused)
{
	(void)unused;
	struct namespace *mine = ns_fork(thread_namespace());
	if (!mine) {
		failure = "selftest: vfs: ns_fork failed";
		return;
	}
	thread_set_namespace(mine);
	ns_unref(mine);

	check(vfs_mount(tree, "test-tree", "/n", BIND_REPLACE) == 0
	      && resolve("/n/a/only_a") == 0,
	      "selftest: vfs: mounting a tree");
	check(resolve("/n/a/../b/./only_b") == 0 && resolve("//n///b") == 0 && resolve("/..") == 0,
	      "selftest: vfs: lexical path cleaning");
	check(resolve("/n/a/missing") == -ENOENT, "selftest: vfs: a missing name is ENOENT");
	check(resolve("/dev/cons/x") == -ENOTDIR, "selftest: vfs: walking through a device is ENOTDIR");
	check(resolve("n/a") == -EINVAL, "selftest: vfs: a relative path is EINVAL");

	check(vfs_bind("/n/b", "/n/a", BIND_AFTER) == 0 && resolve("/n/a/only_b") == 0,
	      "selftest: vfs: bind -a makes a union");
	check(resolve("/n/a/x/from_a") == 0 && resolve("/n/a/x/from_b") == -ENOENT,
	      "selftest: vfs: bind -a searches the original first");
	check(vfs_bind("/n/b", "/n/a", BIND_BEFORE) == 0 /* now b, a, b */
	      && resolve("/n/a/x/from_b") == 0 && resolve("/n/a/x/from_a") == -ENOENT,
	      "selftest: vfs: bind -b searches the new directory first");
	check(count_entries("/n/a") == 6,
	      "selftest: vfs: a union lists every member's entries");
	check(vfs_unbind("/n/a") == 0 && resolve("/n/a/only_b") == -ENOENT
	      && resolve("/n/a/only_a") == 0,
	      "selftest: vfs: unbind restores the original");
	check(vfs_bind("/dev/cons", "/n/a", BIND_REPLACE) == -ENOTDIR,
	      "selftest: vfs: a file cannot be bound over a directory");
	check(vfs_bind("/dev", "/n/a", BIND_REPLACE) == 0 && resolve("/n/a/cons") == 0,
	      "selftest: vfs: bind replaces");
	check(count_entries("/dev") >= 3, "selftest: vfs: devfs lists the devices");
}

void vfs_selftest(void)
{
	size_t heap_before = heap_used();
	size_t baseline = sched_thread_count();

	tree = ramfs_create();
	if (!tree || ramfs_mkdir(tree, "a/x/from_a") || ramfs_mkdir(tree, "a/only_a")
	    || ramfs_mkdir(tree, "b/x/from_b") || ramfs_mkdir(tree, "b/only_b")) {
		panic("selftest: vfs: cannot build the test tree");
	}
	failure = 0;
	if (!thread_create("vfs-test", private_namespace_test, 0)) {
		panic("selftest: vfs: cannot start the test thread");
	}
	uint64_t deadline = timer_uptime_ms() + 2000;
	while (sched_thread_count() > baseline) {
		if (timer_uptime_ms() > deadline) {
			panic("selftest: vfs: test thread did not finish within 2 s");
		}
		thread_yield();
	}
	if (failure) {
		panic(failure);
	}

	/* The test thread is gone, and its namespace with it. */
	if (resolve("/n/a") != -ENOENT || resolve("/dev/cons") != 0) {
		panic("selftest: vfs: a private namespace's binds leaked into its parent's");
	}
	ramfs_destroy(tree); /* panics on any leaked vnode reference */
	if (heap_used() != heap_before) {
		panic("selftest: vfs: the test leaked heap memory");
	}
}
