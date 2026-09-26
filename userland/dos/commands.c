/* ManiDOS's internal commands (dos.h). Each gets its words in argv
 * (argv[0] the command's name) and the raw rest of the line in
 * cmd_rest, for the commands that take text (ECHO, SET, PROMPT...).
 * They write to standard output and read standard input, which
 * main.c points at a file or a pipe for redirection and pipelines. */
#include "dos.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READ_ONLY "Access denied - ManiOS's drives are read-only"

int keyboard_fd;

/* --- helpers --- */

void print_count(unsigned long long n, int width)
{
	char digits[32], out[48];
	int len = snprintf(digits, sizeof(digits), "%llu", n), o = 0;
	for (int i = 0; i < len; i++) {
		if (i && (len - i) % 3 == 0) {
			out[o++] = ',';
		}
		out[o++] = digits[i];
	}
	out[o] = '\0';
	printf("%*s", width, out);
}

void reader_init(struct line_reader *r, int fd)
{
	r->fd = fd;
	r->len = r->pos = 0;
	r->eof = false;
}

char *reader_line(struct line_reader *r, char *out, size_t size)
{
	size_t n = 0;
	for (;;) {
		if (r->pos == r->len) {
			if (r->eof) {
				break;
			}
			long got = read(r->fd, r->buf, sizeof(r->buf));
			if (got <= 0) {
				r->eof = true;
				break;
			}
			r->len = (int)got;
			r->pos = 0;
		}
		char c = r->buf[r->pos++];
		if (c == '\n') {
			out[n] = '\0';
			return out;
		}
		if (c != '\r' && n + 1 < size) {
			out[n++] = c;
		}
	}
	if (!n) {
		return NULL;
	}
	out[n] = '\0';
	return out;
}

static int rows = 0, cols = 0;

int terminal_fd = 1;

/* Asks the terminal its size once: ManiDE's term (a pipe) answers
 * ESC [ 18 t. The console is 80x25, and a serial line can't be asked
 * without its answer showing up as typing: 80x24 there. */
static void ask_size(void)
{
	if (rows) {
		return;
	}
	rows = 24;
	cols = 80;
	struct zkt_dirent kb;
	if (fstat(keyboard_fd, &kb) < 0 || kb.type != ZKT_TYPE_PIPE) {
		return;
	}
	fflush(stdout);
	write(terminal_fd, "\033[18t", 5);
	struct zkt_pollfd pf = { keyboard_fd, 0 };
	char buf[32];
	if (poll(&pf, 1, 200) > 0) {
		long n = read(keyboard_fd, buf, sizeof(buf) - 1);
		buf[n > 0 ? n : 0] = '\0';
		char *p = strstr(buf, "\033[8;");
		if (p) {
			p += 4;
			int r = (int)strtol(p, &p, 10), c = *p == ';' ? (int)strtol(p + 1, &p, 10) : 0;
			if (*p == 't' && r >= 5 && c >= 20) {
				rows = r;
				cols = c;
			}
		}
	}
}

int screen_rows(void)
{
	ask_size();
	return rows;
}

int screen_cols(void)
{
	ask_size();
	return cols;
}

void wait_enter(void)
{
	char c;
	fflush(stdout);
	while (read(keyboard_fd, &c, 1) == 1 && c != '\n' && c != '\r') {
	}
}

/* The arguments that aren't switches, and whether switch `letter` is
 * set ("/W", "/w", or among "/W/P"). */
static bool opt(int argc, char **argv, char letter)
{
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] != '/') {
			continue;
		}
		for (const char *p = argv[i]; *p; p++) {
			if (*p == '/' && toupper((unsigned char)p[1]) == letter) {
				return true;
			}
		}
	}
	return false;
}

static int plain_args(int argc, char **argv, char **out)
{
	int n = 0;
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] != '/') {
			out[n++] = argv[i];
		}
	}
	return n;
}

static bool bad_switch(int argc, char **argv, const char *allowed)
{
	for (int i = 1; i < argc; i++) {
		for (const char *p = argv[i]; argv[i][0] == '/' && *p; p++) {
			if (*p == '/' && (!p[1] || !strchr(allowed, toupper((unsigned char)p[1])))) {
				printf("Invalid switch - %s\n", argv[i]);
				return true;
			}
		}
	}
	return false;
}

/* The label ManiDOS gives a drive, and its serial number if it has one. */
static void volume(int d, char *label, size_t size, uint32_t *serial, bool *has_serial,
                   unsigned long long *free_bytes)
{
	struct fat_volume v;
	*has_serial = false;
	*free_bytes = 0;
	label[0] = '\0';
	if (drives[d].device[0] && fat_open(drives[d].device, &v) == 0) {
		strlcpy(label, v.label, size);
		*serial = v.serial;
		*has_serial = v.has_serial;
		*free_bytes = (unsigned long long)v.free_clusters * v.cluster_size;
	} else if (d == 0) {
		strlcpy(label, "MANIOS-BOOT", size);
	} else if (d == 'Z' - 'A') {
		strlcpy(label, "MANIOS", size);
	}
}

static void print_volume(int d)
{
	char label[16];
	uint32_t serial = 0;
	bool has_serial;
	unsigned long long free_bytes;
	volume(d, label, sizeof(label), &serial, &has_serial, &free_bytes);
	if (label[0]) {
		printf(" Volume in drive %c is %s\n", 'A' + d, label);
	} else {
		printf(" Volume in drive %c has no label\n", 'A' + d);
	}
	if (has_serial) {
		printf(" Volume Serial Number is %04lX-%04lX\n", (unsigned long)(serial >> 16),
		       (unsigned long)(serial & 0xFFFF));
	}
}

/* NAME and EXT as DIR shows them. */
static void dos_name(const char *name, char *base, char *ext)
{
	char up[ZKT_NAME_MAX + 1];
	strlcpy(up, name, sizeof(up));
	upper(up);
	char *dot = strrchr(up, '.');
	if (dot && dot != up && strlen(dot + 1) <= 3 && (size_t)(dot - up) <= 8) {
		*dot = '\0';
		strcpy(base, up);
		strcpy(ext, dot + 1);
	} else {
		strcpy(base, up);
		ext[0] = '\0';
	}
}

/* --- DIR --- */

struct listing {
	struct zkt_dirent *e;
	int n, cap;
	const char *pattern;
};

static void collect(const struct zkt_dirent *e, void *arg)
{
	struct listing *l = arg;
	if (!wild_match(l->pattern, e->name)) {
		return;
	}
	if (l->n == l->cap) {
		int cap = l->cap ? l->cap * 2 : 32;
		struct zkt_dirent *grown = realloc(l->e, (size_t)cap * sizeof(*grown));
		if (!grown) {
			return;
		}
		l->e = grown;
		l->cap = cap;
	}
	l->e[l->n++] = *e;
}

static int shown_lines;

/* A line of output, pausing each screen with /P. */
static void paged(bool pause)
{
	if (pause && ++shown_lines >= screen_rows() - 1) {
		printf("Press Enter to continue . . .");
		wait_enter();
		shown_lines = 0;
	}
}

struct dir_totals {
	unsigned long files, dirs;
	unsigned long long bytes;
};

static void dir_list(int d, const char *path, const char *pattern, bool wide, bool bare,
                     bool pause, bool sub, struct dir_totals *t, bool *found)
{
	char where[256], shown[PATH_MAX_DOS + 4];
	dos_to_manios(d, path, where, sizeof(where));
	struct listing l = { .pattern = pattern };
	each_entry(where, collect, &l);
	bool all = !strcmp(pattern, "*") || !strcmp(pattern, "*.*");
	bool root = !strcmp(path, "\\");
	dos_display(d, path, shown, sizeof(shown));
	if (l.n || (all && !root)) {
		*found = true;
		if (!bare) {
			printf(" Directory of %s\n\n", shown);
			paged(pause);
			paged(pause);
		}
	}
	unsigned long files = 0, dirs = 0;
	unsigned long long bytes = 0;
	int per_row = wide ? screen_cols() / 16 : 0, in_row = 0;
	if (all && !root && !bare) {
		if (wide) {
			printf("%-16s%-16s", "[.]", "[..]");
			in_row = 2;
		} else {
			printf(".            <DIR>\n..           <DIR>\n");
			paged(pause);
			paged(pause);
		}
		dirs += 2;
	}
	for (int i = 0; i < l.n; i++) {
		const struct zkt_dirent *e = &l.e[i];
		bool is_dir = e->type == ZKT_TYPE_DIR;
		char base[ZKT_NAME_MAX + 1], ext[ZKT_NAME_MAX + 1];
		dos_name(e->name, base, ext);
		if (bare) {
			if (sub) {
				char full[PATH_MAX_DOS + ZKT_NAME_MAX + 4];
				snprintf(full, sizeof(full), "%s%s%s", shown, root ? "" : "\\", e->name);
				printf("%s\n", upper(full));
			} else {
				printf("%s%s%s\n", base, ext[0] ? "." : "", ext);
			}
			paged(pause);
		} else if (wide) {
			char cell[ZKT_NAME_MAX + 4];
			snprintf(cell, sizeof(cell), is_dir ? "[%s%s%s]" : "%s%s%s", base, ext[0] ? "." : "",
			         ext);
			printf("%-16s", cell);
			if (++in_row == per_row) {
				putchar('\n');
				paged(pause);
				in_row = 0;
			}
		} else if (strlen(base) > 8) {
			printf("%-12s ", base);
			if (is_dir) {
				printf("  <DIR>\n");
			} else {
				print_count(e->size, 13);
				putchar('\n');
			}
			paged(pause);
		} else {
			printf("%-8s %-3s", base, ext);
			if (is_dir) {
				printf("    <DIR>\n");
			} else {
				print_count(e->size, 14);
				putchar('\n');
			}
			paged(pause);
		}
		if (is_dir) {
			dirs++;
		} else {
			files++;
			bytes += e->size;
		}
	}
	if (wide && in_row) {
		putchar('\n');
		paged(pause);
	}
	if ((l.n || (all && !root)) && !bare) {
		printf("%9lu file(s) ", files);
		print_count(bytes, 14);
		printf(" bytes\n");
		paged(pause);
		if (sub) {
			putchar('\n');
			paged(pause);
		}
	}
	t->files += files;
	t->dirs += dirs;
	t->bytes += bytes;
	free(l.e);

	if (!sub) {
		return;
	}
	/* Then each subdirectory, with the same pattern. */
	struct listing subs = { .pattern = "*" };
	each_entry(where, collect, &subs);
	for (int i = 0; i < subs.n; i++) {
		if (subs.e[i].type != ZKT_TYPE_DIR) {
			continue;
		}
		char next[PATH_MAX_DOS];
		snprintf(next, sizeof(next), "%s%s%s", root ? "" : path, "\\", subs.e[i].name);
		upper(next);
		dir_list(d, next, pattern, wide, bare, pause, true, t, found);
	}
	free(subs.e);
}

static int cmd_dir(int argc, char **argv)
{
	if (bad_switch(argc, argv, "WBPS")) {
		return 1;
	}
	char *args[ARGS_MAX_DOS];
	int n = plain_args(argc, argv, args);
	const char *target = n ? args[0] : ".";
	bool wide = opt(argc, argv, 'W'), bare = opt(argc, argv, 'B'), pause = opt(argc, argv, 'P');
	bool sub = opt(argc, argv, 'S');
	int d;
	char path[PATH_MAX_DOS], where[256], pattern[64] = "*";
	char dirpart[PATH_MAX_DOS], name[64];
	/* A directory is listed whole; otherwise the last part is a pattern. */
	if (dos_resolve(target, &d, path) < 0) {
		printf("Invalid drive specification\n");
		return 1;
	}
	dos_to_manios(d, path, where, sizeof(where));
	if (has_wild(target) || dos_type(where, NULL) != ZKT_TYPE_DIR) {
		split_last(target, dirpart, sizeof(dirpart), name, sizeof(name));
		if (dos_resolve(dirpart[0] ? dirpart : ".", &d, path) < 0) {
			printf("Invalid drive specification\n");
			return 1;
		}
		if (!dirpart[0] && isalpha((unsigned char)target[0]) && target[1] == ':') {
			dos_resolve(target, &d, path); /* "C:NAME" */
			split_last(path, dirpart, sizeof(dirpart), name, sizeof(name));
			strlcpy(path, dirpart[0] ? dirpart : "\\", sizeof(path));
		}
		strlcpy(pattern, name[0] ? name : "*", sizeof(pattern));
		upper(pattern);
		dos_to_manios(d, path, where, sizeof(where));
		if (dos_type(where, NULL) != ZKT_TYPE_DIR) {
			printf("Path not found\n");
			return 1;
		}
	}
	shown_lines = 0;
	if (!bare) {
		print_volume(d);
		paged(pause);
		paged(pause);
	}
	struct dir_totals t = { 0 };
	bool found = false;
	dir_list(d, path, pattern, wide, bare, pause, sub, &t, &found);
	if (!found) {
		if (!bare) {
			putchar('\n');
		}
		printf("File not found\n");
		return 1;
	}
	if (!bare) {
		char label[16];
		uint32_t serial;
		bool has_serial;
		unsigned long long free_bytes;
		volume(d, label, sizeof(label), &serial, &has_serial, &free_bytes);
		if (sub) {
			printf("Total files listed:\n%9lu file(s) ", t.files);
			print_count(t.bytes, 14);
			printf(" bytes\n");
		}
		printf("%9lu dir(s)  ", t.dirs);
		print_count(free_bytes, 14);
		printf(" bytes free\n");
	}
	return 0;
}

/* --- CD, drives --- */

static int cmd_cd(int argc, char **argv)
{
	char *args[ARGS_MAX_DOS];
	int n = plain_args(argc, argv, args);
	int d;
	char path[PATH_MAX_DOS], where[256], shown[PATH_MAX_DOS + 4];
	if (!n || (strlen(args[0]) == 2 && args[0][1] == ':')) {
		if (dos_resolve(n ? args[0] : ".", &d, path) < 0) {
			printf("Invalid drive specification\n");
			return 1;
		}
		dos_display(d, drives[d].cwd, shown, sizeof(shown));
		printf("%s\n", shown);
		return 0;
	}
	if (dos_resolve(args[0], &d, path) < 0) {
		printf("Invalid drive specification\n");
		return 1;
	}
	dos_to_manios(d, path, where, sizeof(where));
	if (dos_type(where, NULL) != ZKT_TYPE_DIR) {
		printf("Invalid directory\n");
		return 1;
	}
	strlcpy(drives[d].cwd, path, sizeof(drives[d].cwd));
	return 0;
}

static int cmd_drives(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	for (int d = 0; d < 26; d++) {
		if (!drives[d].present) {
			continue;
		}
		struct fat_volume v;
		char what[48];
		if (drives[d].device[0] && fat_open(drives[d].device, &v) == 0) {
			snprintf(what, sizeof(what), "FAT%d, %lu KiB", v.bits,
			         (unsigned long)((unsigned long long)v.clusters * v.cluster_size / 1024));
		} else {
			snprintf(what, sizeof(what), "%s", drives[d].kind);
		}
		printf("%c:  %-22s %s%s%s\n", 'A' + d, what, drives[d].root,
		       drives[d].device[0] ? "  on " : "", drives[d].device);
	}
	return 0;
}

static int cmd_truename(int argc, char **argv)
{
	int d;
	char path[PATH_MAX_DOS], where[256], shown[PATH_MAX_DOS + 4];
	if (dos_resolve(argc > 1 ? argv[1] : ".", &d, path) < 0) {
		printf("Invalid drive specification\n");
		return 1;
	}
	dos_display(d, path, shown, sizeof(shown));
	dos_to_manios(d, path, where, sizeof(where));
	printf("%s = %s\n", shown, where);
	return 0;
}

/* --- files --- */

/* Calls fn for each file a (possibly wildcard) name gives: the number
 * found. */
typedef int (*file_fn)(const char *manios_path, const char *shown, void *arg);

struct each_file {
	int d;
	char dir[PATH_MAX_DOS];
	const char *pattern;
	file_fn fn;
	void *arg;
	int found, rc;
};

static void each_file_entry(const struct zkt_dirent *e, void *arg)
{
	struct each_file *f = arg;
	if (e->type == ZKT_TYPE_DIR || !wild_match(f->pattern, e->name)) {
		return;
	}
	char path[PATH_MAX_DOS + ZKT_NAME_MAX + 2], where[256], shown[PATH_MAX_DOS + ZKT_NAME_MAX + 6];
	snprintf(path, sizeof(path), "%s%s%s", strcmp(f->dir, "\\") ? f->dir : "", "\\", e->name);
	upper(path);
	dos_to_manios(f->d, path, where, sizeof(where));
	dos_display(f->d, path, shown, sizeof(shown));
	f->found++;
	f->rc |= f->fn(where, shown, f->arg);
}

static int each_file(const char *name, file_fn fn, void *arg, int *rc)
{
	int d;
	char path[PATH_MAX_DOS], where[256], shown[PATH_MAX_DOS + 4];
	if (!has_wild(name)) {
		if (dos_resolve(name, &d, path) < 0) {
			return -1;
		}
		dos_to_manios(d, path, where, sizeof(where));
		dos_display(d, path, shown, sizeof(shown));
		int type = dos_type(where, NULL);
		if (type < 0) {
			return 0;
		}
		if (type == ZKT_TYPE_DIR) {
			*rc |= 2; /* a directory, not a file */
			return 0;
		}
		*rc |= fn(where, shown, arg);
		return 1;
	}
	char dirpart[PATH_MAX_DOS], pattern[64];
	split_last(name, dirpart, sizeof(dirpart), pattern, sizeof(pattern));
	struct each_file f = { .pattern = pattern, .fn = fn, .arg = arg };
	if (dos_resolve(dirpart[0] ? dirpart : ".", &f.d, f.dir) < 0) {
		return -1;
	}
	dos_to_manios(f.d, f.dir, where, sizeof(where));
	each_entry(where, each_file_entry, &f);
	*rc |= f.rc;
	return f.found;
}

static int copy_fd(int in, int out)
{
	char buf[4096];
	long n;
	fflush(stdout);
	while ((n = read(in, buf, sizeof(buf))) > 0) {
		for (long done = 0; done < n;) {
			long w = write(out, buf + done, (size_t)(n - done));
			if (w <= 0) {
				return -1;
			}
			done += w;
		}
	}
	return n < 0 ? -1 : 0;
}

static int type_one(const char *where, const char *shown, void *arg)
{
	(void)shown;
	(void)arg;
	int fd = open(where, OREAD);
	if (fd < 0) {
		return 1;
	}
	int rc = copy_fd(fd, 1);
	close(fd);
	return rc < 0;
}

static int cmd_type(int argc, char **argv)
{
	if (argc < 2) {
		printf("Required parameter missing\n");
		return 1;
	}
	int status = 0;
	for (int i = 1; i < argc; i++) {
		int rc = 0, found = each_file(argv[i], type_one, NULL, &rc);
		if (found < 0) {
			printf("Invalid drive specification\n");
			status = 1;
		} else if (rc & 2) {
			printf("Access denied - %s is a directory\n", upper(argv[i]));
			status = 1;
		} else if (!found) {
			printf("File not found - %s\n", upper(argv[i]));
			status = 1;
		}
	}
	return status;
}

/* COPY to CON, NUL or a device; files can't be written. */
struct copy_to {
	int out;
	bool show_names;
	int copied;
};

static int copy_one(const char *where, const char *shown, void *arg)
{
	struct copy_to *c = arg;
	if (c->show_names) {
		printf("%s\n", shown);
	}
	int fd = open(where, OREAD);
	if (fd < 0) {
		return 1;
	}
	int rc = c->out >= 0 ? copy_fd(fd, c->out) : 0;
	close(fd);
	c->copied += rc == 0;
	return rc < 0;
}

static int cmd_copy(int argc, char **argv)
{
	char *args[ARGS_MAX_DOS];
	int n = plain_args(argc, argv, args);
	if (!n) {
		printf("Required parameter missing\n");
		return 1;
	}
	struct copy_to c = { .out = -1, .show_names = has_wild(args[0]) };
	char dest[256];
	bool to_null = n > 1 && !strcasecmp(args[n - 1], "NUL");
	if (n > 1 && !strcasecmp(args[n - 1], "CON")) {
		c.out = 1;
	} else if (n > 1 && !to_null) {
		/* A device (Z:\DEV\...) takes writes; files and directories don't. */
		if (dos_manios(args[n - 1], dest, sizeof(dest)) < 0) {
			printf("Invalid drive specification\n");
			return 1;
		}
		if (dos_type(dest, NULL) != ZKT_TYPE_DEVICE || (c.out = open(dest, OWRITE)) < 0) {
			printf("%s\n", READ_ONLY);
			return 1;
		}
	} else if (n == 1) {
		printf("%s\n", READ_ONLY);
		return 1;
	}
	int status = 0;
	for (int i = 0; i < (n > 1 ? n - 1 : 1); i++) {
		int rc = 0, found = each_file(args[i], copy_one, &c, &rc);
		if (found <= 0 || (rc & 2)) {
			printf("File not found - %s\n", upper(args[i]));
			status = 1;
		}
	}
	if (c.out > 1) {
		close(c.out);
	}
	printf("%9d file(s) copied\n", c.copied);
	return status;
}

static int cmd_read_only(int argc, char **argv)
{
	(void)argc;
	printf("%s: %s can't work yet\n", READ_ONLY, argv[0]);
	return 1;
}

static int cmd_no_disk_writes(int argc, char **argv)
{
	(void)argc;
	printf("%s isn't in ManiDOS: it would write to a disk, and ManiOS's drives are read-only\n",
	       argv[0]);
	return 1;
}

/* --- VOL, CHKDSK, TREE --- */

static int drive_arg(int argc, char **argv)
{
	char *args[ARGS_MAX_DOS];
	int n = plain_args(argc, argv, args), d;
	char path[PATH_MAX_DOS];
	if (!n) {
		return current;
	}
	if (dos_resolve(args[0], &d, path) < 0) {
		printf("Invalid drive specification\n");
		return -1;
	}
	return d;
}

static int cmd_vol(int argc, char **argv)
{
	int d = drive_arg(argc, argv);
	if (d < 0) {
		return 1;
	}
	print_volume(d);
	return 0;
}

static int cmd_chkdsk(int argc, char **argv)
{
	if (bad_switch(argc, argv, "VF")) {
		return 1;
	}
	int d = drive_arg(argc, argv);
	if (d < 0) {
		return 1;
	}
	if (opt(argc, argv, 'F')) {
		printf("CHKDSK /F can't correct anything: ManiOS's drives are read-only. Checking only.\n\n");
	}
	if (!drives[d].device[0]) {
		printf("CHKDSK checks FAT disks; drive %c is the %s\n", 'A' + d, drives[d].kind);
		return 1;
	}
	int problems = fat_check(drives[d].device, (char)('A' + d), opt(argc, argv, 'V'));
	if (problems < 0) {
		printf("Drive %c is not a FAT12 or FAT16 disk\n", 'A' + d);
		return 1;
	}
	return problems ? 1 : 0;
}

struct tree {
	int d;
	bool files;
	char prefix[128];
};

static void tree_dir(struct tree *t, const char *path)
{
	char where[256];
	dos_to_manios(t->d, path, where, sizeof(where));
	struct listing l = { .pattern = "*" };
	each_entry(where, collect, &l);
	int dirs = 0, seen = 0;
	for (int i = 0; i < l.n; i++) {
		dirs += l.e[i].type == ZKT_TYPE_DIR;
	}
	if (t->files) {
		bool any = false;
		for (int i = 0; i < l.n; i++) {
			if (l.e[i].type != ZKT_TYPE_DIR) {
				char name[ZKT_NAME_MAX + 1];
				strlcpy(name, l.e[i].name, sizeof(name));
				printf("%s%s   %s\n", t->prefix, dirs ? "|" : " ", upper(name));
				any = true;
			}
		}
		if (any) {
			printf("%s%s\n", t->prefix, dirs ? "|" : "");
		}
	}
	for (int i = 0; i < l.n; i++) {
		if (l.e[i].type != ZKT_TYPE_DIR) {
			continue;
		}
		bool last = ++seen == dirs;
		char name[ZKT_NAME_MAX + 1], next[PATH_MAX_DOS];
		strlcpy(name, l.e[i].name, sizeof(name));
		upper(name);
		printf("%s%s%s\n", t->prefix, last ? "\\---" : "+---", name);
		snprintf(next, sizeof(next), "%s\\%s", strcmp(path, "\\") ? path : "", name);
		size_t len = strlen(t->prefix);
		strlcat(t->prefix, last ? "    " : "|   ", sizeof(t->prefix));
		tree_dir(t, next);
		t->prefix[len] = '\0';
	}
	free(l.e);
}

static int cmd_tree(int argc, char **argv)
{
	if (bad_switch(argc, argv, "FA")) {
		return 1;
	}
	char *args[ARGS_MAX_DOS];
	int n = plain_args(argc, argv, args);
	struct tree t = { .files = opt(argc, argv, 'F') };
	char path[PATH_MAX_DOS], where[256], shown[PATH_MAX_DOS + 4];
	if (dos_resolve(n ? args[0] : ".", &t.d, path) < 0) {
		printf("Invalid drive specification\n");
		return 1;
	}
	dos_to_manios(t.d, path, where, sizeof(where));
	if (dos_type(where, NULL) != ZKT_TYPE_DIR) {
		printf("Invalid path - %s\n", upper(args[0]));
		return 1;
	}
	char label[16];
	uint32_t serial = 0;
	bool has_serial;
	unsigned long long free_bytes;
	volume(t.d, label, sizeof(label), &serial, &has_serial, &free_bytes);
	printf("Directory PATH listing%s%s\n", label[0] ? " for Volume " : "", label);
	if (has_serial) {
		printf("Volume serial number is %04lX-%04lX\n", (unsigned long)(serial >> 16),
		       (unsigned long)(serial & 0xFFFF));
	}
	dos_display(t.d, path, shown, sizeof(shown));
	printf("%s\n", shown);
	tree_dir(&t, path);
	return 0;
}

/* --- FIND, SORT, MORE: filters --- */

struct find {
	char text[LINE_MAX_DOS];
	bool invert, count, number, ignore_case;
	bool matched;
};

static bool contains(const char *line, const char *text, bool ignore_case)
{
	size_t n = strlen(text);
	for (const char *p = line; *p; p++) {
		if (ignore_case ? !strncasecmp(p, text, n) : !strncmp(p, text, n)) {
			return true;
		}
	}
	return !n;
}

static void find_in(struct find *f, int fd, const char *shown)
{
	struct line_reader r;
	char line[LINE_MAX_DOS * 4];
	unsigned long count = 0, number = 0;
	reader_init(&r, fd);
	if (shown && !f->count) {
		printf("\n---------- %s\n", shown);
	}
	while (reader_line(&r, line, sizeof(line))) {
		number++;
		if (contains(line, f->text, f->ignore_case) == f->invert) {
			continue;
		}
		f->matched = true;
		count++;
		if (!f->count) {
			if (f->number) {
				printf("[%lu]", number);
			}
			printf("%s\n", line);
		}
	}
	if (f->count) {
		if (shown) {
			printf("---------- %s: %lu\n", shown, count);
		} else {
			printf("%lu\n", count);
		}
	}
}

static int find_file(const char *where, const char *shown, void *arg)
{
	int fd = open(where, OREAD);
	if (fd < 0) {
		return 1;
	}
	find_in(arg, fd, shown);
	close(fd);
	return 0;
}

static int cmd_find(int argc, char **argv)
{
	if (bad_switch(argc, argv, "VCNI")) {
		return 2;
	}
	struct find f = { .invert = opt(argc, argv, 'V'), .count = opt(argc, argv, 'C'),
		              .number = opt(argc, argv, 'N'), .ignore_case = opt(argc, argv, 'I') };
	char *args[ARGS_MAX_DOS];
	int n = plain_args(argc, argv, args);
	if (!n || args[0][0] != '"') {
		printf("FIND: Parameter format not correct\n");
		return 2;
	}
	strlcpy(f.text, args[0] + 1, sizeof(f.text));
	size_t len = strlen(f.text);
	if (len && f.text[len - 1] == '"') {
		f.text[len - 1] = '\0';
	}
	if (n == 1) {
		fflush(stdout);
		find_in(&f, 0, NULL);
	}
	for (int i = 1; i < n; i++) {
		int rc = 0;
		if (each_file(args[i], find_file, &f, &rc) <= 0) {
			printf("File not found - %s\n", upper(args[i]));
			return 2;
		}
	}
	return f.matched ? 0 : 1;
}

static bool sort_reverse;
static int sort_column;

static int sort_cmp(const void *a, const void *b)
{
	const char *x = *(char *const *)a, *y = *(char *const *)b;
	x += (int)strlen(x) > sort_column ? sort_column : (int)strlen(x);
	y += (int)strlen(y) > sort_column ? sort_column : (int)strlen(y);
	int c = strcasecmp(x, y);
	return sort_reverse ? -c : c;
}

static int cmd_sort(int argc, char **argv)
{
	sort_reverse = opt(argc, argv, 'R');
	sort_column = 0;
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '/' && argv[i][1] == '+') {
			sort_column = atoi(argv[i] + 2) - 1;
			sort_column = sort_column < 0 ? 0 : sort_column;
		}
	}
	char *args[ARGS_MAX_DOS];
	int n = plain_args(argc, argv, args), fd = 0;
	if (n) {
		char where[256];
		if (dos_manios(args[0], where, sizeof(where)) < 0 || (fd = open(where, OREAD)) < 0) {
			printf("File not found - %s\n", upper(args[0]));
			return 1;
		}
	}
	struct line_reader r;
	char line[LINE_MAX_DOS * 4];
	char **lines = NULL;
	size_t count = 0, cap = 0;
	reader_init(&r, fd);
	while (reader_line(&r, line, sizeof(line))) {
		if (count == cap) {
			cap = cap ? cap * 2 : 64;
			char **grown = realloc(lines, cap * sizeof(*lines));
			if (!grown) {
				printf("SORT: not enough memory\n");
				return 1;
			}
			lines = grown;
		}
		lines[count++] = strdup(line);
	}
	if (fd) {
		close(fd);
	}
	qsort(lines, count, sizeof(*lines), sort_cmp);
	for (size_t i = 0; i < count; i++) {
		printf("%s\n", lines[i]);
		free(lines[i]);
	}
	free(lines);
	return 0;
}

/* MORE: a screen at a time, from its files or standard input. */
static int more_fd(int fd)
{
	struct line_reader r;
	char line[LINE_MAX_DOS * 4];
	int page = screen_rows() - 1, width = screen_cols(), shown = 0;
	reader_init(&r, fd);
	while (reader_line(&r, line, sizeof(line))) {
		printf("%s\n", line);
		shown += 1 + (int)strlen(line) / (width > 0 ? width : 80);
		if (shown >= page) {
			printf("-- More --");
			wait_enter();
			shown = 0;
		}
	}
	return 0;
}

static int more_file(const char *where, const char *shown, void *arg)
{
	(void)shown;
	(void)arg;
	int fd = open(where, OREAD);
	if (fd < 0) {
		return 1;
	}
	more_fd(fd);
	close(fd);
	return 0;
}

static int cmd_more(int argc, char **argv)
{
	char *args[ARGS_MAX_DOS];
	int n = plain_args(argc, argv, args);
	if (!n) {
		fflush(stdout);
		return more_fd(0);
	}
	for (int i = 0; i < n; i++) {
		int rc = 0;
		if (each_file(args[i], more_file, NULL, &rc) <= 0) {
			printf("File not found - %s\n", upper(args[i]));
			return 1;
		}
	}
	return 0;
}

/* --- the command interpreter's own --- */

const char *cmd_rest;

static int cmd_echo(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *r = cmd_rest;
	if (*r == '.' || *r == ':' || *r == '/') {
		printf("%s\n", r + 1); /* ECHO. is a blank line */
		return 0;
	}
	while (*r == ' ' || *r == '\t') {
		r++;
	}
	if (!*r) {
		printf("ECHO is %s\n", echo_on ? "on" : "off");
	} else if (!strcasecmp(r, "ON") || !strcasecmp(r, "OFF")) {
		echo_on = !strcasecmp(r, "ON");
	} else {
		printf("%s\n", r);
	}
	return 0;
}

static const char *skip_blanks(const char *s)
{
	while (*s == ' ' || *s == '\t') {
		s++;
	}
	return s;
}

static int cmd_set(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *r = skip_blanks(cmd_rest);
	const char *eq = strchr(r, '=');
	if (!*r) {
		env_list();
		return 0;
	}
	if (!eq) {
		const char *v = env_get(r);
		if (!v) {
			printf("Environment variable %s not defined\n", r);
			return 1;
		}
		char name[64];
		strlcpy(name, r, sizeof(name));
		printf("%s=%s\n", upper(name), v);
		return 0;
	}
	char name[64];
	size_t n = (size_t)(eq - r) < sizeof(name) - 1 ? (size_t)(eq - r) : sizeof(name) - 1;
	memcpy(name, r, n);
	name[n] = '\0';
	if (!n) {
		printf("Syntax error\n");
		return 1;
	}
	return env_set(name, eq[1] ? eq + 1 : NULL);
}

static int cmd_path(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *r = skip_blanks(cmd_rest);
	if (*r == '=') {
		r = skip_blanks(r + 1);
	}
	if (!*r) {
		const char *p = env_get("PATH");
		printf(p ? "PATH=%s\n" : "No Path\n", p);
		return 0;
	}
	return env_set("PATH", strcmp(r, ";") ? r : NULL);
}

static int cmd_prompt(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *r = skip_blanks(cmd_rest);
	return env_set("PROMPT", *r ? r : "$P$G");
}

static int cmd_rem(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	return 0;
}

static int cmd_pause(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	printf("Press Enter to continue . . .");
	wait_enter();
	return 0;
}

static int cmd_cls(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	fputs("\033[H\033[2J", stdout);
	return 0;
}

static int cmd_exit(int argc, char **argv)
{
	leaving = true;
	return argc > 1 ? atoi(argv[1]) : 0;
}

static long read_small(const char *path, char *buf, size_t size)
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

static unsigned long stat_value(const char *text, const char *name, int field)
{
	const char *p = strstr(text, name);
	if (!p) {
		return 0;
	}
	char *q = (char *)p + strlen(name);
	unsigned long v = 0;
	for (int i = 0; i <= field; i++) {
		v = strtoul(q, &q, 10);
	}
	return v;
}

/* The word after skipping `n` words of s. */
static const char *word_at(const char *s, int n)
{
	for (int i = 0; i < n; i++) {
		while (*s == ' ') {
			s++;
		}
		while (*s && *s != ' ') {
			s++;
		}
	}
	while (*s == ' ') {
		s++;
	}
	return s;
}

static int cmd_ver(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	char stat[512], version[24] = "";
	read_small("/dev/sysstat", stat, sizeof(stat));
	const char *v = strstr(stat, "version ");
	if (v) {
		strlcpy(version, v + 8, sizeof(version));
		version[strcspn(version, "\n")] = '\0';
	}
	printf("\nManiDOS Version %s, on ManiOS %s (ZKT)\n", DOS_VERSION, version);
	return 0;
}

static int cmd_mem(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	char stat[512], ps[4096];
	read_small("/dev/sysstat", stat, sizeof(stat));
	unsigned long total = stat_value(stat, "\nmemory ", 0), free_kib = stat_value(stat, "\nmemory ", 1);
	printf("\n");
	print_count(total, 12);
	printf("K total memory\n");
	print_count(total - free_kib, 12);
	printf("K used\n");
	print_count(free_kib, 12);
	printf("K free\n");
	/* ManiDOS's own share, from /dev/ps: "PID PPID STATE CPU_MS MEM_KIB NAME". */
	read_small("/dev/ps", ps, sizeof(ps));
	char *save;
	for (char *line = strtok_r(ps, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		if (strtol(line, NULL, 10) == getpid()) {
			printf("\nManiDOS is using %luK.\n", strtoul(word_at(line, 4), NULL, 10));
		}
	}
	return 0;
}

/* Days since 1970-01-01 to a civil date (H. Hinnant's algorithm, as
 * ManiOS's clock program has it). */
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

static int cmd_date_time(int argc, char **argv)
{
	char buf[24];
	read_small("/dev/time", buf, sizeof(buf));
	long t = strtol(buf, NULL, 10);
	bool date = toupper((unsigned char)argv[0][0]) == 'D';
	if (date) {
		static const char *const DAYS[] = { "Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed" };
		int y, m, d;
		civil(t / 86400, &y, &m, &d);
		printf("Current date is %s %02d-%02d-%04d\n", DAYS[(t / 86400) % 7], m, d, y);
	} else {
		long s = t % 86400;
		printf("Current time is %2ld:%02ld:%02ld (UTC)\n", s / 3600, s / 60 % 60, s % 60);
	}
	if (argc > 1) {
		printf("ManiOS keeps the clock: %s can't set it\n", date ? "DATE" : "TIME");
		return 1;
	}
	return 0;
}

static int cmd_help(int argc, char **argv)
{
	if (argc > 1) {
		const struct command *c = find_command(argv[1]);
		if (!c) {
			printf("No help for %s: it isn't a ManiDOS command\n", upper(argv[1]));
			return 1;
		}
		printf("%-9s %s\n", c->name, c->help);
		return 0;
	}
	printf("ManiDOS commands (HELP COMMAND for one):\n\n");
	for (const struct command *c = COMMANDS; c->name; c++) {
		if (c->help[0] != '-') {
			printf("%-9s %s\n", c->name, c->help);
		}
	}
	printf("\nD:        makes drive D current. Other words run a batch file (.BAT) or a\n"
	       "          ManiOS program from the current directory or the PATH (Z:\\BIN).\n"
	       "          |, <, > and >> connect commands and files; EXIT returns to ManiOS.\n");
	return 0;
}

static int cmd_call(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *r = skip_blanks(cmd_rest);
	if (!*r) {
		return 0;
	}
	return run_call(r);
}

static int cmd_goto(int argc, char **argv)
{
	if (argc < 2) {
		printf("Label not found\n");
		return 1;
	}
	batch_goto(argv[1]);
	return 0;
}

static int cmd_shift(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	batch_shift();
	return 0;
}

static int cmd_if(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	return if_command(cmd_rest);
}

static int cmd_for(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	return for_command(cmd_rest);
}

const struct command COMMANDS[] = {
	{ "CALL", cmd_call, "runs a batch file from another, and comes back" },
	{ "CD", cmd_cd, "[D:][PATH]  shows or changes the current directory (also CHDIR)" },
	{ "CHDIR", cmd_cd, "-" },
	{ "CHKDSK", cmd_chkdsk, "[D:] [/V]  checks a FAT disk: its chains, lost clusters, FAT copies" },
	{ "CLS", cmd_cls, "clears the screen" },
	{ "COPY", cmd_copy, "SOURCE CON|NUL|DEVICE  copies files to the screen or a device" },
	{ "DATE", cmd_date_time, "shows the date (UTC)" },
	{ "DEL", cmd_read_only, "deletes files (not yet: the drives are read-only)" },
	{ "DIR", cmd_dir, "[PATH] [/W] [/B] [/P] [/S]  lists a directory" },
	{ "DRIVES", cmd_drives, "lists the drives and where they are in ManiOS" },
	{ "ECHO", cmd_echo, "[ON|OFF|TEXT]  shows text, or turns command echoing on or off" },
	{ "ERASE", cmd_read_only, "-" },
	{ "EXIT", cmd_exit, "leaves ManiDOS" },
	{ "FDISK", cmd_no_disk_writes, "-" },
	{ "FIND", cmd_find, "[/V] [/C] [/N] [/I] \"TEXT\" [FILES]  finds lines with TEXT" },
	{ "FOR", cmd_for, "%V IN (SET) DO COMMAND  runs a command for each item" },
	{ "FORMAT", cmd_no_disk_writes, "-" },
	{ "GOTO", cmd_goto, "LABEL  jumps to :LABEL in a batch file" },
	{ "HELP", cmd_help, "[COMMAND]  this list" },
	{ "IF", cmd_if, "[NOT] ERRORLEVEL N|EXIST F|A==B COMMAND  runs a command if..." },
	{ "LABEL", cmd_no_disk_writes, "-" },
	{ "MD", cmd_read_only, "makes a directory (not yet)" },
	{ "MEM", cmd_mem, "shows the memory in use" },
	{ "MKDIR", cmd_read_only, "-" },
	{ "MORE", cmd_more, "[FILES]  shows text a screen at a time (also: COMMAND | MORE)" },
	{ "PATH", cmd_path, "[DIRS]  shows or sets where programs are looked for" },
	{ "PAUSE", cmd_pause, "waits for Enter" },
	{ "PROMPT", cmd_prompt, "[TEXT]  sets the prompt: $P path, $G >, $D date, $T time, $N drive" },
	{ "RD", cmd_read_only, "removes a directory (not yet)" },
	{ "REM", cmd_rem, "a remark: does nothing" },
	{ "REN", cmd_read_only, "renames files (not yet)" },
	{ "RENAME", cmd_read_only, "-" },
	{ "RMDIR", cmd_read_only, "-" },
	{ "SET", cmd_set, "[NAME=[VALUE]]  shows or sets environment variables" },
	{ "SHIFT", cmd_shift, "moves a batch file's parameters down one" },
	{ "SORT", cmd_sort, "[/R] [/+N] [FILE]  sorts lines" },
	{ "SYS", cmd_no_disk_writes, "-" },
	{ "TIME", cmd_date_time, "shows the time (UTC)" },
	{ "TREE", cmd_tree, "[PATH] [/F]  draws the directory tree" },
	{ "TRUENAME", cmd_truename, "[PATH]  shows a path in full, and where it is in ManiOS" },
	{ "TYPE", cmd_type, "FILES  shows files' contents" },
	{ "VER", cmd_ver, "shows the version" },
	{ "VOL", cmd_vol, "[D:]  shows a disk's label and serial number" },
	{ NULL, NULL, NULL },
};

const struct command *find_command(const char *name)
{
	for (const struct command *c = COMMANDS; c->name; c++) {
		if (!strcasecmp(c->name, name)) {
			return c;
		}
	}
	return NULL;
}
