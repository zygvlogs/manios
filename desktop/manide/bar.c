/* The status bar's facts (DESIGN.md §3), refreshed once a second:
 *
 *   host | ZKT VERSION | up TIME | cpu N% | mem N% | INTERFACE ADDRESS
 *
 * and, on the right, the date and time (UTC). They come from files:
 * /dev/sysname, /dev/sysstat (cpu is the share of the last second the
 * CPU was not idle), /dev/net and /dev/time. */
#include "manide.h"
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REFRESH_MS 1000

static char host[32], left[160], right[32];
static uint32_t last_refresh, last_uptime, last_idle;
static bool sampled;
static int cpu_percent;

/* Reads a small file whole: its length, or -1. */
static long slurp(const char *path, char *buf, size_t size)
{
	int fd = open(path, OREAD);
	if (fd < 0) {
		return -1;
	}
	long n = read(fd, buf, size - 1);
	close(fd);
	buf[n > 0 ? n : 0] = '\0';
	return n;
}

/* Copies the word at s (up to a space or the end of the line); returns
 * what follows it. */
static const char *word(const char *s, char *out, size_t size)
{
	size_t n = 0;
	while (*s == ' ') {
		s++;
	}
	for (; *s && *s != ' ' && *s != '\n'; s++) {
		if (n + 1 < size) {
			out[n++] = *s;
		}
	}
	out[n] = '\0';
	return s;
}

/* "NAME VALUE..." in /dev/sysstat's text: VALUE's field-th number. */
static unsigned long stat_field(const char *text, const char *name, int field)
{
	size_t len = strlen(name);
	for (const char *line = text; line && *line; line = strchr(line, '\n')) {
		if (*line == '\n') {
			line++;
		}
		if (!strncmp(line, name, len) && line[len] == ' ') {
			char *p = (char *)line + len;
			unsigned long v = 0;
			for (int i = 0; i <= field; i++) {
				v = strtoul(p, &p, 10);
			}
			return v;
		}
	}
	return 0;
}

/* Days since 1970-01-01 to a civil date: H. Hinnant's algorithm, as in
 * clock.c. */
static void civil(long days, int *year, int *month, int *day)
{
	long z = days + 719468;
	long era = z / 146097;
	long doe = z - era * 146097;
	long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	long mp = (5 * doy + 2) / 153;
	*day = (int)(doy - (153 * mp + 2) / 5 + 1);
	*month = (int)(mp < 10 ? mp + 3 : mp - 9);
	*year = (int)(yoe + era * 400 + (*month <= 2));
}

static void format_uptime(char *buf, size_t size, unsigned long ms)
{
	unsigned long s = ms / 1000;
	if (s < 60) {
		snprintf(buf, size, "up %lus", s);
	} else if (s < 3600) {
		snprintf(buf, size, "up %lum", s / 60);
	} else if (s < 86400) {
		snprintf(buf, size, "up %luh %lum", s / 3600, s / 60 % 60);
	} else {
		snprintf(buf, size, "up %lud %luh", s / 86400, s / 3600 % 24);
	}
}

/* The first interface other than loopback: "NAME ADDRESS". */
static void network(char *buf, size_t size)
{
	char text[512];
	strlcpy(buf, "no network", size);
	if (slurp("/dev/net", text, sizeof(text)) <= 0) {
		return;
	}
	for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
		char name[16], addr[24];
		word(word(line, name, sizeof(name)), addr, sizeof(addr));
		if (name[0] && addr[0] && strcmp(name, "lo")) {
			char *slash = strchr(addr, '/');
			if (slash) {
				*slash = '\0';
			}
			snprintf(buf, size, "%s %s", name, addr);
			return;
		}
	}
}

void bar_init(void)
{
	if (slurp("/dev/sysname", host, sizeof(host)) <= 0) {
		strlcpy(host, "manios", sizeof(host));
	}
	host[strcspn(host, "\n")] = '\0';
	last_refresh = uptime_ms() - REFRESH_MS;
	bar_update();
}

bool bar_update(void)
{
	uint32_t now = uptime_ms();
	if (now - last_refresh < REFRESH_MS) {
		return false;
	}
	last_refresh = now;

	char stat[512], up[24], net[48], version[24] = "?";
	if (slurp("/dev/sysstat", stat, sizeof(stat)) > 0) {
		const char *v = strstr(stat, "version ");
		if (v) {
			word(v + 8, version, sizeof(version));
		}
	}
	uint32_t uptime = (uint32_t)stat_field(stat, "uptime", 0);
	uint32_t idle = (uint32_t)stat_field(stat, "idle", 0);
	unsigned long total = stat_field(stat, "memory", 0), free_kib = stat_field(stat, "memory", 1);
	if (sampled && uptime - last_uptime > 0) {
		uint32_t span = uptime - last_uptime, rest = idle - last_idle;
		cpu_percent = rest >= span ? 0 : (int)((span - rest) * 100 / span);
	}
	sampled = true;
	last_uptime = uptime;
	last_idle = idle;
	int mem_percent = total ? (int)((total - free_kib) * 100 / total) : 0;
	format_uptime(up, sizeof(up), uptime);
	network(net, sizeof(net));

	char new_left[sizeof(left)], new_right[sizeof(right)];
	snprintf(new_left, sizeof(new_left), "%s | ZKT %s | %s | cpu %d%% | mem %d%% | %s", host, version,
	         up, cpu_percent, mem_percent, net);

	char t[24];
	unsigned long secs = slurp("/dev/time", t, sizeof(t)) > 0 ? strtoul(t, NULL, 10) : 0;
	int year, month, day;
	civil((long)(secs / 86400), &year, &month, &day);
	unsigned long s = secs % 86400;
	snprintf(new_right, sizeof(new_right), "%04d-%02d-%02d %02lu:%02lu:%02lu", year, month, day,
	         s / 3600, s / 60 % 60, s % 60);

	bool changed = strcmp(new_left, left) || strcmp(new_right, right);
	strcpy(left, new_left);
	strcpy(right, new_right);
	return changed;
}

const char *bar_left(void)
{
	return left;
}

const char *bar_right(void)
{
	return right;
}
