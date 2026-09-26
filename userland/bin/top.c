/* top [-b] [-n COUNT] [-d SECONDS] -- the processes, busiest first,
 * with the CPU and memory in use: from /dev/ps and /dev/sysstat.
 *
 * Full screen, redrawn every SECONDS (2 by default), with ANSI escapes:
 * it asks the terminal its size (ESC [ 18 t; 80x24 without an answer).
 * A line that starts with q (q, Enter) or end of file quits, clearing
 * the screen; after COUNT frames the last one stays. -b (batch)
 * prints plain text instead, every process, COUNT times (default once).
 * CPU shares are over the time between two looks, so the first comes
 * half a second after starting. */
#include <manios.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PROCS 96
#define FIRST_LOOK_MS 500

struct proc {
	unsigned long pid, ppid, cpu_ms, mem_kib;
	char state[12], name[32];
	unsigned long share; /* of the CPU since the last look, in tenths of a percent */
};

struct sample {
	unsigned long uptime, idle, total_kib, free_kib;
	struct proc procs[MAX_PROCS];
	int n;
};

static int batch, rows = 24, cols = 80;

static long slurp(const char *path, char *buf, size_t size)
{
	int fd = open(path, OREAD);
	if (fd < 0) {
		buf[0] = '\0';
		return -1;
	}
	size_t got = 0;
	long n;
	while (got < size - 1 && (n = read(fd, buf + got, size - 1 - got)) > 0) {
		got += (size_t)n;
	}
	close(fd);
	buf[got] = '\0';
	return (long)got;
}

static unsigned long stat_field(const char *text, const char *name, int field)
{
	size_t len = strlen(name);
	for (const char *line = text; *line;) {
		if (!strncmp(line, name, len) && line[len] == ' ') {
			char *p = (char *)line + len;
			unsigned long v = 0;
			for (int i = 0; i <= field; i++) {
				v = strtoul(p, &p, 10);
			}
			return v;
		}
		const char *nl = strchr(line, '\n');
		if (!nl) {
			break;
		}
		line = nl + 1;
	}
	return 0;
}

/* Copies a space-delimited word; returns what follows it. */
static char *word(char *s, char *out, size_t size)
{
	size_t n = 0;
	while (*s == ' ') {
		s++;
	}
	for (; *s && *s != ' '; s++) {
		if (n + 1 < size) {
			out[n++] = *s;
		}
	}
	out[n] = '\0';
	return s;
}

static void look(struct sample *s)
{
	static char text[8192];
	slurp("/dev/sysstat", text, sizeof(text));
	s->uptime = stat_field(text, "uptime", 0);
	s->idle = stat_field(text, "idle", 0);
	s->total_kib = stat_field(text, "memory", 0);
	s->free_kib = stat_field(text, "memory", 1);
	slurp("/dev/ps", text, sizeof(text));
	s->n = 0;
	for (char *line = strtok(text, "\n"); line && s->n < MAX_PROCS; line = strtok(NULL, "\n")) {
		struct proc *p = &s->procs[s->n++];
		char *q = line;
		p->pid = strtoul(q, &q, 10);
		p->ppid = strtoul(q, &q, 10);
		q = word(q, p->state, sizeof(p->state));
		p->cpu_ms = strtoul(q, &q, 10);
		p->mem_kib = strtoul(q, &q, 10);
		strlcpy(p->name, *q == ' ' ? q + 1 : q, sizeof(p->name));
		p->share = 0;
	}
}

/* Each process's share of the CPU between two looks. */
static void shares(struct sample *now, const struct sample *before)
{
	unsigned long span = now->uptime - before->uptime;
	for (int i = 0; i < now->n && span; i++) {
		struct proc *p = &now->procs[i];
		unsigned long was = 0;
		for (int k = 0; k < before->n; k++) {
			if (before->procs[k].pid == p->pid) {
				was = before->procs[k].cpu_ms;
			}
		}
		unsigned long used = p->cpu_ms >= was ? p->cpu_ms - was : 0;
		p->share = used * 1000 / span > 1000 ? 1000 : used * 1000 / span;
	}
}

static int busiest_first(const void *a, const void *b)
{
	const struct proc *p = a, *q = b;
	if (p->share != q->share) {
		return p->share < q->share ? 1 : -1;
	}
	return p->pid < q->pid ? -1 : p->pid > q->pid;
}

/* On the full screen, a line is cut to the terminal's width rather
 * than wrapping onto the next. */
static void fit(char *line)
{
	if (!batch && (int)strlen(line) > cols) {
		line[cols] = '\0';
	}
}

/* An escape sequence, only on the full screen. */
static void esc(const char *s)
{
	if (!batch) {
		fputs(s, stdout);
	}
}

/* Ends a line: on the full screen, erasing what was there. */
static void eol(void)
{
	esc("\033[K");
	putchar('\n');
}

static void bar(const char *label, unsigned long permille, const char *note)
{
	int width = cols - 8 - (int)strlen(note) - 2;
	width = width < 10 ? 10 : width > 60 ? 60 : width;
	int filled = (int)(permille * (unsigned long)width / 1000);
	const char *c = permille >= 800 ? "\033[1;31m" : permille >= 500 ? "\033[1;33m" : "\033[1;32m";
	esc("\033[1;36m");
	printf("%-4s", label);
	esc("\033[0m");
	printf("[");
	esc(c);
	for (int i = 0; i < width; i++) {
		putchar(i < filled ? '|' : ' ');
	}
	esc("\033[0m");
	printf("] %s", note);
	eol();
}

static void show(struct sample *now, const struct sample *before)
{
	unsigned long span = now->uptime - before->uptime, rest = now->idle - before->idle;
	unsigned long busy = span && rest < span ? (span - rest) * 1000 / span : 0;
	unsigned long used = now->total_kib - now->free_kib;
	int running = 0;
	for (int i = 0; i < now->n; i++) {
		running += !strcmp(now->procs[i].state, "running") || !strcmp(now->procs[i].state, "ready");
	}
	qsort(now->procs, (size_t)now->n, sizeof(now->procs[0]), busiest_first);

	unsigned long s = now->uptime / 1000;
	char line[160];
	snprintf(line, sizeof(line), "top - up %lu:%02lu:%02lu, %d processes: %d running, %d sleeping",
	         s / 3600, s / 60 % 60, s % 60, now->n, running, now->n - running);
	fit(line);
	esc("\033[H\033[1m");
	fputs(line, stdout);
	esc("\033[0m");
	eol();
	char note[40];
	snprintf(note, sizeof(note), "%lu.%lu%%", busy / 10, busy % 10);
	bar("CPU", busy, note);
	snprintf(note, sizeof(note), "%lu.%luM/%lu.%luM", used / 1024, used % 1024 * 10 / 1024,
	         now->total_kib / 1024, now->total_kib % 1024 * 10 / 1024);
	bar("Mem", now->total_kib ? used * 1000 / now->total_kib : 0, note);
	eol();
	esc("\033[30;46m");
	char head[160];
	snprintf(head, sizeof(head), "%5s %5s %-9s %5s %9s %7s  %-*s", "PID", "PPID", "STATE", "CPU%",
	         "TIME", "MEM", batch ? 0 : cols, "NAME");
	fit(head);
	fputs(head, stdout);
	esc("\033[0m");
	eol();
	int room = batch ? now->n : rows - 6;
	for (int i = 0; i < now->n && i < room; i++) {
		const struct proc *p = &now->procs[i];
		unsigned long cs = p->cpu_ms / 10;
		char time[16], mem[16], line[160];
		snprintf(time, sizeof(time), "%lu:%02lu.%02lu", cs / 6000, cs / 100 % 60, cs % 100);
		snprintf(mem, sizeof(mem), "%luK", p->mem_kib);
		bool active = !strcmp(p->state, "running") || !strcmp(p->state, "ready");
		snprintf(line, sizeof(line), "%5lu %5lu %-9s %3lu.%lu %9s %7s  %s", p->pid, p->ppid, p->state,
		         p->share / 10, p->share % 10, time, mem, p->name);
		fit(line);
		if (active) {
			esc("\033[1;32m");
		}
		fputs(line, stdout);
		if (active) {
			esc("\033[0m");
		}
		eol();
	}
	if (!batch) {
		esc("\033[J");
		printf("\033[%d;1H\033[2mq Enter: quit\033[0m ", rows);
	}
	fflush(stdout);
}

/* Waits up to ms for standard input: what it says about the terminal's
 * size is noted; 1 if it asks to quit. */
static int wait_input(int ms)
{
	struct zkt_pollfd pf = { 0, 0 };
	uint32_t until = uptime_ms() + (uint32_t)ms;
	for (;;) {
		uint32_t now = uptime_ms();
		if ((int32_t)(until - now) <= 0) {
			return 0;
		}
		if (poll(&pf, 1, (int)(until - now)) <= 0) {
			return 0;
		}
		char buf[128];
		long n = read(0, buf, sizeof(buf) - 1);
		if (n <= 0) {
			return 1; /* end of file */
		}
		buf[n] = '\0';
		char *size = strstr(buf, "\033[8;");
		if (size) {
			char *p = size + 4;
			int r = (int)strtol(p, &p, 10);
			int c = *p == ';' ? (int)strtol(p + 1, &p, 10) : 0;
			if (*p == 't' && r >= 8 && c >= 40) {
				rows = r;
				cols = c;
			}
			memmove(size, p + 1, strlen(p + 1) + 1);
		}
		if (buf[strspn(buf, " \t")] == 'q') {
			return 1;
		}
	}
}

int main(int argc, char **argv)
{
	int count = 0, delay_ms = 2000;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-b")) {
			batch = 1;
		} else if (!strcmp(argv[i], "-n") && i + 1 < argc) {
			count = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "-d") && i + 1 < argc) {
			delay_ms = atoi(argv[++i]) * 1000;
		} else {
			fprintf(stderr, "usage: top [-b] [-n COUNT] [-d SECONDS]\n");
			return 2;
		}
	}
	if (batch && !count) {
		count = 1;
	}
	if (delay_ms < 100) {
		delay_ms = 100;
	}
	static struct sample a, b;
	struct sample *before = &a, *now = &b;
	look(before);
	int quit = 0;
	if (!batch) {
		fputs("\033[18t\033[H\033[2J", stdout);
		fflush(stdout);
		quit = wait_input(FIRST_LOOK_MS);
	} else {
		sleep_ms(FIRST_LOOK_MS);
	}
	for (int frame = 0; !quit && (!count || frame < count); frame++) {
		if (frame) {
			if (batch) {
				sleep_ms((uint32_t)delay_ms);
				putchar('\n');
			} else {
				fputs("\033[18t", stdout); /* the size again: the pane may have changed */
				fflush(stdout);
				if ((quit = wait_input(delay_ms))) {
					break;
				}
			}
		}
		look(now);
		shares(now, before);
		show(now, before);
		struct sample *t = before;
		before = now;
		now = t;
	}
	if (!batch && quit) {
		fputs("\033[0m\033[H\033[2J", stdout); /* asked to quit: a clean screen */
	} else if (!batch) {
		printf("\033[0m\033[%d;1H\033[K", rows); /* after COUNT: the last frame stays */
	}
	return 0;
}
