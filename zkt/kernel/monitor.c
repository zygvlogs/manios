#include "monitor.h"
#include "namespace.h"
#include <stdbool.h>
#include <stdint.h>
#include "device.h"
#include "heap.h"
#include "kconsole.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "panic.h"
#include "pmm.h"
#include "sched.h"
#include "timer.h"
#include "vfs.h"

#define LINE_MAX 128
#define ARGS_MAX 8

struct command {
	const char *name;
	const char *usage;
	void (*run)(int argc, char **argv);
};

static void cmd_help(int argc, char **argv);

static void cmd_echo(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		kprintf(i + 1 < argc ? "%s " : "%s", argv[i]);
	}
	kprintf("\n");
}

static void cmd_uptime(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	uint32_t ms = (uint32_t)timer_uptime_ms();
	kprintf("uptime: %lu.%03lu s\n", ms / 1000, ms % 1000);
}

static void cmd_mem(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	kprintf("physical: %lu KiB free of %lu KiB usable\n",
	        pmm_free_frames() * 4, pmm_usable_frames() * 4);
	kprintf("heap: %lu bytes in use\n", heap_used());
}

static void cmd_threads(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	struct thread_info info[32];
	size_t n = sched_snapshot(info, 32);
	kprintf("  id  state     name\n");
	for (size_t i = 0; i < n; i++) {
		kprintf("%4lu  %-8s  %s\n", info[i].id, info[i].state, info[i].name);
	}
}

static void cmd_devices(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	for (struct device *d = device_next(0); d; d = device_next(d)) {
		if (d->class == DEVICE_BLOCK) {
			kprintf("  %-8s block  %lu x %lu bytes (%lu MiB)\n", d->name, d->block_count,
			        d->block_size, d->block_count / (1024 * 1024 / d->block_size));
		} else {
			kprintf("  %-8s char   %s%s\n", d->name,
			        d->char_ops->read ? "read " : "", d->char_ops->write ? "write" : "");
		}
	}
}

/* Decimal only; returns false on anything else or on overflow. */
static bool parse_u32(const char *s, uint32_t *out)
{
	uint32_t v = 0;
	if (!*s) {
		return false;
	}
	for (; *s; s++) {
		if (*s < '0' || *s > '9' || v > (0xFFFFFFFFu - (uint32_t)(*s - '0')) / 10) {
			return false;
		}
		v = v * 10 + (uint32_t)(*s - '0');
	}
	*out = v;
	return true;
}

static void hexdump(const uint8_t *data, size_t len)
{
	for (size_t off = 0; off < len; off += 16) {
		char hex[16 * 3 + 1];
		char text[17];
		for (size_t i = 0; i < 16; i++) {
			uint8_t b = data[off + i];
			ksnprintf(hex + i * 3, 4, "%02x ", b);
			text[i] = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
		}
		text[16] = '\0';
		kprintf("%04lx  %s %s\n", (uint32_t)off, hex, text);
	}
}

static void cmd_read(int argc, char **argv)
{
	uint32_t lba;
	struct device *d = argc == 3 ? device_find(argv[1]) : 0;
	if (argc != 3 || !parse_u32(argv[2], &lba)) {
		kprintf("usage: read DEVICE BLOCK\n");
		return;
	}
	if (!d || d->class != DEVICE_BLOCK) {
		kprintf("read: %s: not a block device\n", argv[1]);
		return;
	}
	uint8_t *buf = kmalloc(d->block_size);
	if (!buf) {
		kprintf("read: %s\n", kstrerror(ENOMEM));
		return;
	}
	int rc = device_read_blocks(d, lba, 1, buf);
	if (rc == 0) {
		hexdump(buf, d->block_size);
	} else {
		kprintf("read: %s block %lu: %s\n", d->name, lba, kstrerror(rc));
	}
	kfree(buf);
}

static const char *type_name(enum vnode_type t)
{
	return t == VNODE_DIR ? "dir" : t == VNODE_DEVICE ? "dev" : "file";
}

static void cmd_ls(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/";
	struct file *f;
	struct dirent d;
	int rc = vfs_open(path, &f);
	if (rc == 0) {
		while ((rc = vfs_readdir(f, &d)) == 1) {
			kprintf("  %-4s %8lu  %s%s\n", type_name(d.type), d.size, d.name,
			        d.type == VNODE_DIR ? "/" : "");
		}
		vfs_close(f);
	}
	if (rc < 0) {
		kprintf("ls: %s: %s\n", path, kstrerror(rc));
	}
}

/* Reads a file in chunks, handing each to fn; `limit` caps the bytes. */
static long read_all(const char *path, uint32_t limit,
                     void (*fn)(const uint8_t *buf, size_t len, void *ctx), void *ctx)
{
	struct file *f;
	int rc = vfs_open(path, &f);
	if (rc) {
		return rc;
	}
	uint8_t buf[256];
	uint32_t total = 0;
	long n = 0;
	while (total < limit) {
		size_t want = limit - total < sizeof(buf) ? limit - total : sizeof(buf);
		n = vfs_read(f, buf, want);
		if (n <= 0) {
			break;
		}
		fn(buf, (size_t)n, ctx);
		total += (uint32_t)n;
	}
	vfs_close(f);
	return n < 0 ? n : (long)total;
}

static void print_chunk(const uint8_t *buf, size_t len, void *ctx)
{
	(void)ctx;
	kconsole_write_n((const char *)buf, len);
}

static void cmd_cat(int argc, char **argv)
{
	if (argc != 2) {
		kprintf("usage: cat PATH\n");
		return;
	}
	long rc = read_all(argv[1], 0xFFFFFFFFu, print_chunk, 0);
	if (rc < 0) {
		kprintf("cat: %s: %s\n", argv[1], kstrerror((int)rc));
	}
}

/* FNV-1a, 32-bit: simple enough that tests can recompute it. */
static void fnv1a_chunk(const uint8_t *buf, size_t len, void *ctx)
{
	uint32_t *h = ctx;
	for (size_t i = 0; i < len; i++) {
		*h = (*h ^ buf[i]) * 16777619u;
	}
}

static void cmd_sum(int argc, char **argv)
{
	uint32_t limit = 0xFFFFFFFFu;
	if (argc < 2 || argc > 3 || (argc == 3 && !parse_u32(argv[2], &limit))) {
		kprintf("usage: sum PATH [BYTES]\n");
		return;
	}
	uint32_t hash = 2166136261u;
	long n = read_all(argv[1], limit, fnv1a_chunk, &hash);
	if (n < 0) {
		kprintf("sum: %s: %s\n", argv[1], kstrerror((int)n));
	} else {
		kprintf("%s: %lu bytes, fnv1a %08lx\n", argv[1], (uint32_t)n, hash);
	}
}

static void cmd_bind(int argc, char **argv)
{
	enum bind_flag flag = BIND_REPLACE;
	int i = 1;
	if (argc == 4 && strcmp(argv[1], "-a") == 0) {
		flag = BIND_AFTER;
		i = 2;
	} else if (argc == 4 && strcmp(argv[1], "-b") == 0) {
		flag = BIND_BEFORE;
		i = 2;
	} else if (argc != 3) {
		kprintf("usage: bind [-a|-b] NEW OLD\n");
		return;
	}
	int rc = vfs_bind(argv[i], argv[i + 1], flag);
	if (rc) {
		kprintf("bind: %s\n", kstrerror(rc));
	}
}

static void cmd_unbind(int argc, char **argv)
{
	if (argc != 2) {
		kprintf("usage: unbind OLD\n");
		return;
	}
	int rc = vfs_unbind(argv[1]);
	if (rc) {
		kprintf("unbind: %s: %s\n", argv[1], kstrerror(rc));
	}
}

static void print_mount(const char *path, const struct location *loc, void *ctx)
{
	(void)ctx;
	kprintf("  %s =", path);
	for (size_t i = 0; i < loc->count; i++) {
		kprintf(" %s", loc->label[i]);
	}
	kprintf("\n");
}

static void cmd_ns(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	ns_foreach(thread_namespace(), print_mount, 0);
}

static const struct command COMMANDS[] = {
	{ "help", "list commands", cmd_help },
	{ "echo", "ARGS... - print the arguments", cmd_echo },
	{ "uptime", "time since boot", cmd_uptime },
	{ "mem", "physical memory and heap usage", cmd_mem },
	{ "threads", "list kernel threads", cmd_threads },
	{ "devices", "list registered devices", cmd_devices },
	{ "read", "DEVICE BLOCK - hex dump one block", cmd_read },
	{ "ls", "[PATH] - list a directory", cmd_ls },
	{ "cat", "PATH - print a file", cmd_cat },
	{ "sum", "PATH [BYTES] - size and FNV-1a hash", cmd_sum },
	{ "bind", "[-a|-b] NEW OLD - bind NEW onto OLD", cmd_bind },
	{ "unbind", "OLD - undo binds on OLD", cmd_unbind },
	{ "ns", "show this namespace's binds", cmd_ns },
	{ 0, 0, 0 },
};

static void cmd_help(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	kprintf("commands:\n");
	for (const struct command *c = COMMANDS; c->name; c++) {
		kprintf("  %-10s %s\n", c->name, c->usage);
	}
}

static struct device *cons;

/* Reads one line with echo and backspace. Terminals send '\r' for
 * Enter, some follow it with '\n'; either ends the line, but a '\n'
 * right after a '\r' is swallowed rather than taken as an empty line. */
static void read_line(char *line, size_t max)
{
	static bool after_cr;
	size_t len = 0;

	for (;;) {
		char c;
		if (device_read(cons, &c, 1) != 1) {
			continue;
		}
		if (c == '\n' && after_cr) {
			after_cr = false;
			continue;
		}
		after_cr = c == '\r';

		if (c == '\r' || c == '\n') {
			kconsole_write("\n");
			line[len] = '\0';
			return;
		}
		if (c == '\b' || c == 0x7F) {
			if (len) {
				len--;
				kconsole_write("\b \b");
			}
			continue;
		}
		if (c >= 0x20 && c < 0x7F && len + 1 < max) {
			line[len++] = c;
			kconsole_putc(c);
		}
	}
}

static int split(char *line, char **argv)
{
	int argc = 0;
	char *p = line;
	while (argc < ARGS_MAX) {
		while (*p == ' ') {
			*p++ = '\0';
		}
		if (!*p) {
			break;
		}
		argv[argc++] = p;
		while (*p && *p != ' ') {
			p++;
		}
	}
	return argc;
}

void monitor_main(void *unused)
{
	(void)unused;
	cons = device_find("cons");
	if (!cons) {
		panic("monitor: no console device");
	}

	for (;;) {
		char line[LINE_MAX];
		char *argv[ARGS_MAX];

		kconsole_write("ZKT> ");
		read_line(line, sizeof(line));
		int argc = split(line, argv);
		if (argc == 0) {
			continue;
		}

		const struct command *c = COMMANDS;
		while (c->name && strcmp(c->name, argv[0]) != 0) {
			c++;
		}
		if (c->name) {
			c->run(argc, argv);
		} else {
			kprintf("unknown command: %s (try help)\n", argv[0]);
		}
	}
}
