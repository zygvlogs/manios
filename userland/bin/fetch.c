/* fetch [-p] -- this system at a glance, beside the ManiOS logo, in the
 * manner of neofetch. Colours are ANSI escapes (ManiDE's term and the
 * text console show them); -p leaves them out. */
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOGO_W 16
#define GAP "   "

/* An M: amber above, cyan below. */
static const char *const LOGO[] = {
	"MM          MM",
	"MMM        MMM",
	"MMMM      MMMM",
	"MM MM    MM MM",
	"MM  MM  MM  MM",
	"MM   MMMM   MM",
	"MM    MM    MM",
	"MM          MM",
	"MM          MM",
	"MM          MM",
};
#define LOGO_LINES (int)(sizeof(LOGO) / sizeof(LOGO[0]))
#define LOGO_SPLIT 5

static int colour = 1;

/* An SGR escape, or nothing without colours. */
#define SGR(codes) (colour ? "\033[" codes "m" : "")

static long slurp(const char *path, char *buf, size_t size)
{
	int fd = open(path, OREAD);
	if (fd < 0) {
		buf[0] = '\0';
		return -1;
	}
	long n = read(fd, buf, size - 1);
	close(fd);
	buf[n > 0 ? n : 0] = '\0';
	return n;
}

/* The rest of the line after "NAME " in /dev/sysstat's text. */
static const char *stat_line(const char *text, const char *name, char *out, size_t size)
{
	size_t len = strlen(name);
	out[0] = '\0';
	for (const char *line = text; *line;) {
		if (!strncmp(line, name, len) && line[len] == ' ') {
			const char *v = line + len + 1;
			size_t n = strcspn(v, "\n");
			n = n < size - 1 ? n : size - 1;
			memcpy(out, v, n);
			out[n] = '\0';
			break;
		}
		const char *nl = strchr(line, '\n');
		if (!nl) {
			break;
		}
		line = nl + 1;
	}
	return out;
}

static int exists(const char *path)
{
	int fd = open(path, OREAD);
	if (fd >= 0) {
		close(fd);
	}
	return fd >= 0;
}

static int programs(void)
{
	struct zkt_dirent e;
	int fd = open("/bin", OREAD), n = 0;
	while (fd >= 0 && read(fd, &e, sizeof(e)) == sizeof(e)) {
		n++;
	}
	if (fd >= 0) {
		close(fd);
	}
	return n;
}

static void uptime_text(char *buf, size_t size, unsigned long ms)
{
	unsigned long m = ms / 60000, h = m / 60, d = h / 24;
	if (d) {
		snprintf(buf, size, "%lu day%s, %lu hour%s", d, d == 1 ? "" : "s", h % 24,
		         h % 24 == 1 ? "" : "s");
	} else if (h) {
		snprintf(buf, size, "%lu hour%s, %lu min%s", h, h == 1 ? "" : "s", m % 60,
		         m % 60 == 1 ? "" : "s");
	} else if (m) {
		snprintf(buf, size, "%lu min%s", m, m == 1 ? "" : "s");
	} else {
		snprintf(buf, size, "%lu secs", ms / 1000);
	}
}

/* The first network interface other than loopback: "NAME ADDRESS". */
static int network(char *buf, size_t size)
{
	char text[512];
	if (slurp("/dev/net", text, sizeof(text)) <= 0) {
		return 0;
	}
	for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
		char *sp = strchr(line, ' ');
		if (!sp || !strncmp(line, "lo ", 3)) {
			continue;
		}
		*sp = '\0';
		char *addr = sp + 1;
		addr[strcspn(addr, "/ ")] = '\0';
		snprintf(buf, size, "%s %s", line, addr);
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "-p")) {
		colour = 0;
	} else if (argc != 1) {
		fprintf(stderr, "usage: fetch [-p]\n");
		return 2;
	}

	char host[40], stat[512], version[32], up[40], cpu[64], mem[40], net[48], res[32];
	char lines[16][96];
	int n = 0;
	slurp("/dev/sysname", host, sizeof(host));
	host[strcspn(host, "\n")] = '\0';
	slurp("/dev/sysstat", stat, sizeof(stat));
	stat_line(stat, "version", version, sizeof(version));
	stat_line(stat, "uptime", up, sizeof(up));
	uptime_text(up, sizeof(up), strtoul(up, NULL, 10));
	stat_line(stat, "cpu", cpu, sizeof(cpu));
	stat_line(stat, "memory", mem, sizeof(mem));
	char *p;
	unsigned long total = strtoul(mem, &p, 10), free_kib = strtoul(p, NULL, 10);
	snprintf(mem, sizeof(mem), "%lu MiB / %lu MiB", (total - free_kib) / 1024, total / 1024);
	int desktop = exists("/dev/wsys");

	const char *key = SGR("1;36"), *off = SGR("0");
#define ADD(label, ...)                                                                   \
	do {                                                                              \
		int k = snprintf(lines[n], sizeof(lines[n]), "%s%s%s: ", key, label, off); \
		snprintf(lines[n] + k, sizeof(lines[n]) - (size_t)k, __VA_ARGS__);        \
		n++;                                                                      \
	} while (0)

	snprintf(lines[n++], sizeof(lines[0]), "%s%s%s", SGR("1;33"), host, off);
	memset(lines[n], '-', strlen(host));
	lines[n++][strlen(host)] = '\0';
	ADD("OS", "ManiOS %s i386", version);
	ADD("Kernel", "ZKT %s", version);
	ADD("Uptime", "%s", up);
	ADD("Programs", "%d (/bin)", programs());
	ADD("Shell", "sh");
	if (desktop) {
		ADD("DE", "ManiDE");
		char fb[64];
		if (slurp("/dev/fbctl", fb, sizeof(fb)) > 0) {
			unsigned long w = strtoul(fb, &p, 10), h = strtoul(p, NULL, 10);
			snprintf(res, sizeof(res), "%lux%lu", w, h);
			ADD("Resolution", "%s", res);
		}
		ADD("Terminal", "term");
	}
	ADD("CPU", "%s", cpu);
	ADD("Memory", "%s", mem);
	if (network(net, sizeof(net))) {
		ADD("Network", "%s", net);
	}
	lines[n++][0] = '\0';
	if (colour) {
		/* The palette: normal colours, then bright. */
		for (int row = 0; row < 2; row++) {
			int k = 0;
			for (int i = 0; i < 8; i++) {
				k += snprintf(lines[n] + k, sizeof(lines[n]) - (size_t)k, "\033[%dm   ",
				              (row ? 100 : 40) + i);
			}
			snprintf(lines[n] + k, sizeof(lines[n]) - (size_t)k, "\033[0m");
			n++;
		}
	}

	int total_lines = n > LOGO_LINES ? n : LOGO_LINES;
	for (int i = 0; i < total_lines; i++) {
		if (i < LOGO_LINES) {
			printf("%s%-*s%s", i < LOGO_SPLIT ? SGR("1;33") : SGR("1;36"), LOGO_W, LOGO[i], off);
		} else {
			printf("%-*s", LOGO_W, "");
		}
		printf("%s%s\n", GAP, i < n ? lines[i] : "");
	}
	return 0;
}
