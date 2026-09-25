#include "monitor.h"
#include "namespace.h"
#include <stdbool.h>
#include <stdint.h>
#include "cmdline.h"
#include "device.h"
#include "heap.h"
#include "kconsole.h"
#include "kerrno.h"
#include "zkt_abi.h"
#include "kprintf.h"
#include "kstring.h"
#include "panic.h"
#include "pmm.h"
#include "net.h"
#include "pci.h"
#include "process.h"
#include "zrp.h"
#include "sched.h"
#include "timer.h"
#include "vfs.h"

#define LINE_MAX CONSOLE_LINE_MAX
#define ARGS_MAX 16

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
	int rc = vfs_open(path, OREAD, &f);
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
	int rc = vfs_open(path, OREAD, &f);
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

/* Runs a user program in the monitor's namespace, with the console as
 * its standard input and output, and waits for it. */
static void cmd_run(int argc, char **argv)
{
	if (argc < 2) {
		kprintf("usage: run PATH [ARGS...]\n");
		return;
	}
	int pid = process_spawn(argv[1], argc - 1, argv + 1, 0);
	if (pid < 0) {
		kprintf("run: %s: %s\n", argv[1], kstrerror(pid));
		return;
	}
	int status;
	process_wait(0, pid, &status);
	if (status & ZKT_WAIT_KILLED) {
		kprintf("run: %s killed (vector %d)\n", argv[1], ZKT_WAIT_VECTOR(status));
	} else if (status) {
		kprintf("run: %s exited with status %d\n", argv[1], ZKT_WAIT_CODE(status));
	}
}

static void cmd_pci(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	for (const struct pci_device *d = pci_next(0); d; d = pci_next(d)) {
		kprintf("%02x:%02x.%x  %04x:%04x  class %02x.%02x.%02x  irq %u\n", d->bus, d->dev, d->fn,
		        d->vendor, d->device, d->class, d->subclass, d->prog_if, d->irq);
	}
}

static void cmd_net(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	char a[16], m[16], g[16];
	for (struct netif *ifc = netif_first(); ifc; ifc = ifc->next) {
		kprintf("%-4s %s netmask %s gateway %s", ifc->name, ip_format(ifc->ip, a),
		        ip_format(ifc->netmask, m), ip_format(ifc->gateway, g));
		if (!ifc->loopback) {
			kprintf(" mac %02x:%02x:%02x:%02x:%02x:%02x", ifc->mac[0], ifc->mac[1], ifc->mac[2],
			        ifc->mac[3], ifc->mac[4], ifc->mac[5]);
		}
		kprintf("\n     rx %lu frames (%lu dropped), tx %lu frames (%lu errors)\n",
		        (unsigned long)ifc->rx_frames, (unsigned long)ifc->rx_dropped,
		        (unsigned long)ifc->tx_frames, (unsigned long)ifc->tx_errors);
	}
	kprintf("ip: %lu in, %lu bad, %lu fragments, %lu not ours; udp: %lu in, %lu bad, %lu no port;"
	        " echo replies %lu; zrp retransmits %lu\n",
	        (unsigned long)net_stats.ip_in, (unsigned long)net_stats.ip_bad,
	        (unsigned long)net_stats.ip_fragments, (unsigned long)net_stats.ip_not_ours,
	        (unsigned long)net_stats.udp_in, (unsigned long)net_stats.udp_bad,
	        (unsigned long)net_stats.udp_no_port, (unsigned long)net_stats.icmp_echo_replied,
	        (unsigned long)zrp_client_retransmits());
}

static void cmd_arp(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	uint32_t ips[16];
	uint8_t macs[16][ETH_ALEN];
	size_t n = arp_snapshot(ips, macs, 16);
	for (size_t i = 0; i < n; i++) {
		char a[16];
		kprintf("%-15s %02x:%02x:%02x:%02x:%02x:%02x\n", ip_format(ips[i], a), macs[i][0],
		        macs[i][1], macs[i][2], macs[i][3], macs[i][4], macs[i][5]);
	}
}

static void cmd_ping(int argc, char **argv)
{
	uint32_t ip, count = 3;
	if (argc < 2 || argc > 3 || !ip_parse(argv[1], &ip, 0) || (argc == 3 && !parse_u32(argv[2], &count))) {
		kprintf("usage: ping A.B.C.D [COUNT]\n");
		return;
	}
	for (uint32_t seq = 1; seq <= count; seq++) {
		long rtt = icmp_ping(ip, (uint16_t)seq, 1000);
		if (rtt < 0) {
			kprintf("ping %s: seq %lu: %s\n", argv[1], (unsigned long)seq, kstrerror((int)rtt));
		} else {
			kprintf("ping %s: seq %lu: reply in %ld ms\n", argv[1], (unsigned long)seq, rtt);
		}
		if (seq < count) {
			timer_sleep_ms(200);
		}
	}
}

static void cmd_mount(int argc, char **argv)
{
	if (argc < 3 || argc > 4) {
		kprintf("usage: mount DIAL OLD [ANAME]  (DIAL: udp!A.B.C.D[!PORT])\n");
		return;
	}
	struct vnode *root;
	char label[40];
	int rc = zrp_mount(argv[1], argc == 4 ? argv[3] : "", &root, label, sizeof(label));
	if (rc == 0) {
		rc = vfs_mount(root, label, argv[2], BIND_REPLACE);
		vnode_unref(root);
	}
	if (rc) {
		kprintf("mount: %s: %s\n", argv[1], kstrerror(rc));
	}
}

static void cmd_export(int argc, char **argv)
{
	struct zrp_server *srv = zrp_main_server();
	if (argc < 2 || argc > 3) {
		kprintf("usage: export PATH [NAME]  (serves PATH over ZRP, udp port %d)\n", ZRP_PORT);
		return;
	}
	const char *name = argc == 3 ? argv[2] : "";
	int rc = srv ? zrp_export(srv, name, argv[1], 0) : -ENODEV;
	if (rc) {
		kprintf("export: %s: %s\n", argv[1], kstrerror(rc));
	} else {
		kprintf("exporting %s as \"%s\" on udp port %d\n", argv[1], name, ZRP_PORT);
	}
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
	{ "run", "PATH [ARGS...] - run a user program", cmd_run },
	{ "pci", "list PCI devices", cmd_pci },
	{ "net", "network interfaces and counters", cmd_net },
	{ "arp", "the ARP cache", cmd_arp },
	{ "ping", "A.B.C.D [COUNT] - ICMP echo", cmd_ping },
	{ "mount", "DIAL OLD [ANAME] - mount a ZRP server", cmd_mount },
	{ "export", "PATH [NAME] - serve PATH over ZRP", cmd_export },
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

/* One line from the cooked console (kconsole.c does the editing and
 * echo), without its newline. End of file reads as an empty line. */
static void read_line(char *line, size_t max)
{
	long n = device_read(cons, line, max - 1);
	if (n < 0) {
		n = 0;
	}
	if (n > 0 && line[n - 1] == '\n') {
		n--;
	}
	line[n] = '\0';
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

#define SHELL "/bin/sh"

/* rc=PATH on the command line: a script the shell runs first, which
 * sets a machine up for its role -- a CPU server starts cpud (M13). */
static void run_rc(void)
{
	char path[VFS_PATH_MAX + 1];
	if (!cmdline_get("rc", path, sizeof(path))) {
		return;
	}
	char *argv[] = { SHELL, path, 0 };
	int status, pid = process_spawn(SHELL, 2, argv, 0);
	if (pid < 0) {
		kprintf("console: rc: cannot start %s: %s\n", SHELL, kstrerror(pid));
	} else if (process_wait(0, pid, &status) == pid && status != 0) {
		kprintf("console: rc: %s: status %d\n", path, status);
	}
}

void console_main(void *unused)
{
	run_rc();
	char *argv[] = { SHELL, 0 };
	int pid = process_spawn(SHELL, 1, argv, 0);
	if (pid < 0) {
		kprintf("console: cannot start %s: %s\n", SHELL, kstrerror(pid));
	} else {
		int status;
		process_wait(0, pid, &status);
		kprintf("console: the shell has exited; this is the kernel monitor "
		        "(run %s to go back)\n", SHELL);
	}
	monitor_main(unused);
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
