/* dos [/C COMMAND] -- ManiDOS, a disk operating system for ManiOS
 * (dos.h, docs/dos.md). Interactive, it runs A:\AUTOEXEC.BAT and then
 * reads commands at a prompt until EXIT; with /C it runs one command
 * line and exits with its errorlevel.
 *
 * A command line is a pipeline of commands joined by |, each with its
 * redirections (< FILE, > FILE, >> FILE; the drives are read-only, so
 * output goes to CON, NUL or a device). A command is an internal one
 * (commands.c), a drive change ("D:"), a batch file (.BAT), or a
 * ManiOS program found in the current directory or on the PATH. A
 * ManiOS program is given its arguments with DOS paths turned into
 * ManiOS ones -- a word with a drive letter, or naming a file or
 * directory that exists -- and starts in the current directory. An
 * internal command in the middle of a pipeline runs in a second
 * ManiDOS (dos /C), which finds its drive and directory from the
 * ManiOS directory it starts in. */
#include "dos.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_STAGES 8
#define ENV_MAX 64

int errorlevel;
bool echo_on = true;
bool leaving;

/* --- the environment --- */

static char *env[ENV_MAX];

static int env_find(const char *name, size_t len)
{
	for (int i = 0; i < ENV_MAX; i++) {
		if (env[i] && !strncasecmp(env[i], name, len) && env[i][len] == '=') {
			return i;
		}
	}
	return -1;
}

const char *env_get(const char *name)
{
	int i = env_find(name, strlen(name));
	return i < 0 ? NULL : env[i] + strlen(name) + 1;
}

int env_set(const char *name, const char *value)
{
	size_t len = strlen(name);
	int i = env_find(name, len);
	if (i >= 0) {
		free(env[i]);
		env[i] = NULL;
	}
	if (!value) {
		return 0;
	}
	for (i = 0; i < ENV_MAX && env[i]; i++) {
	}
	if (i == ENV_MAX) {
		printf("Out of environment space\n");
		return 1;
	}
	env[i] = malloc(len + strlen(value) + 2);
	if (!env[i]) {
		printf("Out of environment space\n");
		return 1;
	}
	strcpy(env[i], name);
	upper(env[i]);
	env[i][len] = '=';
	strcpy(env[i] + len + 1, value);
	return 0;
}

void env_list(void)
{
	for (int i = 0; i < ENV_MAX; i++) {
		if (env[i]) {
			printf("%s\n", env[i]);
		}
	}
}

/* --- the prompt --- */

static long read_number(const char *path)
{
	char buf[24];
	int fd = open(path, OREAD);
	long n = fd >= 0 ? read(fd, buf, sizeof(buf) - 1) : -1;
	if (fd >= 0) {
		close(fd);
	}
	buf[n > 0 ? n : 0] = '\0';
	return strtol(buf, NULL, 10);
}

void print_prompt(void)
{
	const char *p = env_get("PROMPT");
	char shown[PATH_MAX_DOS + 4];
	if (!p) {
		p = "$P$G";
	}
	for (; *p; p++) {
		if (*p != '$' || !p[1]) {
			putchar(*p);
			continue;
		}
		long t;
		switch (toupper((unsigned char)*++p)) {
		case 'P':
			dos_display(current, drives[current].cwd, shown, sizeof(shown));
			fputs(shown, stdout);
			break;
		case 'N':
			putchar('A' + current);
			break;
		case 'G':
			putchar('>');
			break;
		case 'L':
			putchar('<');
			break;
		case 'B':
			putchar('|');
			break;
		case 'Q':
			putchar('=');
			break;
		case '$':
			putchar('$');
			break;
		case '_':
			putchar('\n');
			break;
		case 'E':
			putchar('\033');
			break;
		case 'H':
			putchar('\b');
			break;
		case 'V':
			printf("ManiDOS Version %s", DOS_VERSION);
			break;
		case 'T':
			t = read_number("/dev/time") % 86400;
			printf("%2ld:%02ld:%02ld", t / 3600, t / 60 % 60, t % 60);
			break;
		case 'D':
			t = read_number("/dev/time") / 86400;
			{
				/* Days to a date, as DATE does it (H. Hinnant's algorithm). */
				long z = t + 719468, era = z / 146097, doe = z - era * 146097;
				long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
				long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
				long d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
				printf("%02ld-%02ld-%04ld", m, d, yoe + era * 400 + (m <= 2));
			}
			break;
		default:
			break;
		}
	}
	fflush(stdout);
}

/* --- words --- */

/* Splits text into words at blanks; quoted parts stay together, quotes
 * kept (FIND wants its "text" whole) unless `strip`. */
static int split_words(char *text, char **argv, int max, bool strip)
{
	int n = 0;
	char *p = text;
	while (n < max - 1) {
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		if (!*p) {
			break;
		}
		argv[n++] = p;
		char *out = p;
		bool quoted = false;
		for (; *p && (quoted || (*p != ' ' && *p != '\t')); p++) {
			if (*p == '"') {
				quoted = !quoted;
				if (strip) {
					continue;
				}
			}
			*out++ = *p;
		}
		bool end = !*p;
		*out = '\0';
		if (end) {
			break;
		}
		p++;
	}
	argv[n] = NULL;
	return n;
}

/* Removes what looks like a terminal's escape sequence (an answer to a
 * query that arrived late) from a typed line. */
static void strip_escapes(char *s)
{
	char *out = s;
	for (char *p = s; *p;) {
		if (*p == '\033') {
			p++;
			if (*p == '[') {
				p++;
				while (*p && !(*p >= 0x40 && *p <= 0x7E)) {
					p++;
				}
			}
			if (*p) {
				p++;
			}
			continue;
		}
		*out++ = *p++;
	}
	*out = '\0';
}

/* --- running things --- */

static void cd_to_current(void)
{
	char where[256];
	dos_to_manios(current, drives[current].cwd, where, sizeof(where));
	chdir(where);
}

/* Starts a program with its standard input and output on in and out. */
static int spawn_with(const char *path, char **argv, int in, int out)
{
	int saved_in = in != 0 ? dup(0) : -1, saved_out = out != 1 ? dup(1) : -1;
	fflush(stdout);
	if (in != 0) {
		dup2(in, 0);
	}
	if (out != 1) {
		dup2(out, 1);
	}
	cd_to_current();
	int pid = spawn(path, argv);
	if (saved_in >= 0) {
		dup2(saved_in, 0);
		close(saved_in);
	}
	if (saved_out >= 0) {
		dup2(saved_out, 1);
		close(saved_out);
	}
	return pid;
}

static int wait_for(int pid, const char *name)
{
	int status;
	if (wait(pid, &status) != pid) {
		return 255;
	}
	if (status & ZKT_WAIT_KILLED) {
		printf("%s ended with a CPU exception (vector %d)\n", name, ZKT_WAIT_VECTOR(status));
		return 255;
	}
	return ZKT_WAIT_CODE(status);
}

enum found { NOT_FOUND, PROGRAM, BATCH, DOS_PROGRAM };

/* Looks for NAME, then NAME.BAT, in one directory (a DOS path). */
static enum found look_in(const char *dir, const char *name, char *where, size_t size,
                          char *shown, size_t ssize)
{
	/* ManiOS programs have no extension; .COM and .EXE are looked for
	 * so that a DOS program can be refused by name. */
	static const char *const EXTENSIONS[] = { "", ".BAT", ".COM", ".EXE" };
	const char *dot = strrchr(name, '.');
	for (int i = 0; i < 4; i++) {
		if (i && dot) {
			break; /* a name with an extension is looked for as it is */
		}
		char path[PATH_MAX_DOS + 16];
		snprintf(path, sizeof(path), "%s%s%s%s", dir,
		         dir[0] && !strchr("\\:", dir[strlen(dir) - 1]) ? "\\" : "", name, EXTENSIONS[i]);
		int d;
		char full[PATH_MAX_DOS];
		if (dos_resolve(path, &d, full) < 0) {
			return NOT_FOUND;
		}
		dos_to_manios(d, full, where, size);
		dos_display(d, full, shown, ssize);
		if (dos_type(where, NULL) != ZKT_TYPE_FILE) {
			continue;
		}
		const char *ext = strrchr(full, '.');
		if (ext && (!strcmp(ext, ".COM") || !strcmp(ext, ".EXE"))) {
			return DOS_PROGRAM;
		}
		return ext && !strcmp(ext, ".BAT") ? BATCH : PROGRAM;
	}
	return NOT_FOUND;
}

static enum found find_program(const char *name, char *where, size_t size, char *shown, size_t ssize)
{
	/* With a drive or a directory, only there. */
	if (strpbrk(name, "\\/:")) {
		char dir[PATH_MAX_DOS], base[64];
		split_last(name, dir, sizeof(dir), base, sizeof(base));
		return look_in(dir, base, where, size, shown, ssize);
	}
	enum found f = look_in("", name, where, size, shown, ssize);
	const char *path = env_get("PATH");
	if (f != NOT_FOUND || !path) {
		return f;
	}
	char dirs[LINE_MAX_DOS];
	strlcpy(dirs, path, sizeof(dirs));
	char *save;
	for (char *d = strtok_r(dirs, ";", &save); d; d = strtok_r(NULL, ";", &save)) {
		f = look_in(d, name, where, size, shown, ssize);
		if (f != NOT_FOUND) {
			return f;
		}
	}
	return NOT_FOUND;
}

/* A word for a ManiOS program: a DOS path becomes a ManiOS one if it
 * has a drive letter, or names something that exists. */
static char *translate(const char *word, char *buf, size_t size)
{
	if (word[0] == '-' || has_wild(word) || !word[0]) {
		return (char *)word;
	}
	bool drive = isalpha((unsigned char)word[0]) && word[1] == ':';
	if (!drive && word[0] == '/') {
		return (char *)word; /* a switch, or already a ManiOS path */
	}
	if (dos_manios(word, buf, size) < 0) {
		return (char *)word;
	}
	return drive || dos_type(buf, NULL) >= 0 ? buf : (char *)word;
}

struct stage {
	char text[LINE_MAX_DOS];
	char *in_file, *out_file;
	bool append;
};

/* Opens a redirection's file: -1 (said why) if it can't be. Writing
 * makes the file if it isn't there, empties it for ">", and starts
 * where it ends for ">>". */
static int open_redirect(const char *name, bool output, bool append)
{
	char where[256];
	if (!strcasecmp(name, "NUL")) {
		return open("/dev/null", output ? OWRITE : OREAD);
	}
	if (!strcasecmp(name, "CON")) {
		return dup(output ? 1 : keyboard_fd);
	}
	if (dos_manios(name, where, sizeof(where)) < 0) {
		printf("Invalid drive specification\n");
		return -1;
	}
	int type = dos_type(where, NULL);
	if (!output) {
		int fd = type == ZKT_TYPE_FILE || type == ZKT_TYPE_DEVICE ? open(where, OREAD) : -1;
		if (fd < 0) {
			printf("File not found\n");
		}
		return fd;
	}
	if (type == ZKT_TYPE_DIR) {
		printf("Access denied - %s is a directory\n", name);
		return -1;
	}
	int fd = open(where, type == ZKT_TYPE_DEVICE ? OWRITE
	                                             : OWRITE | O_CREAT | (append ? 0 : O_TRUNC));
	if (fd < 0) {
		printf("%s\n", denied(errno));
		return -1;
	}
	if (append && type != ZKT_TYPE_DEVICE) {
		lseek(fd, 0, SEEK_END);
	}
	return fd;
}

/* The command's name and the rest: an internal command's name may run
 * straight into what follows ("CD..", "DIR/W", "ECHO."). */
static const struct command *internal(char *text, char **rest)
{
	char name[16];
	size_t n = 0;
	while (isalpha((unsigned char)text[n]) && n < sizeof(name) - 1) {
		name[n] = text[n];
		n++;
	}
	name[n] = '\0';
	const struct command *c = n ? find_command(name) : NULL;
	char next = text[n];
	if (!c || (next && !strchr(" \t./\\:;=,+\"", next))) {
		return NULL;
	}
	*rest = text + n;
	if (**rest == ' ' || **rest == '\t') {
		(*rest)++; /* ECHO's text starts after one blank */
	}
	return c;
}

/* Runs one command in this process, its input and output on in and out
 * (a batch file too). */
static int run_here(struct stage *s, int in, int out)
{
	int saved_in = in != 0 ? dup(0) : -1, saved_out = out != 1 ? dup(1) : -1;
	fflush(stdout);
	if (in != 0) {
		dup2(in, 0);
	}
	if (out != 1) {
		dup2(out, 1);
	}
	char *text = s->text, *rest;
	while (*text == ' ' || *text == '\t') {
		text++;
	}
	int rc = 0;
	const struct command *c;
	char *argv[ARGS_MAX_DOS + 1];
	if (isalpha((unsigned char)text[0]) && text[1] == ':' && !text[2 + strspn(text + 2, " \t")]) {
		int d = toupper((unsigned char)text[0]) - 'A';
		if (drives[d].present) {
			current = d;
		} else {
			printf("Invalid drive specification\n");
			rc = 1;
		}
	} else if ((c = internal(text, &rest))) {
		char words[LINE_MAX_DOS];
		cmd_rest = rest;
		strlcpy(words, rest, sizeof(words));
		argv[0] = (char *)c->name;
		split_words(words, argv + 1, ARGS_MAX_DOS, false);
		int argc = 1;
		while (argv[argc]) {
			argc++;
		}
		rc = c->run(argc, argv);
	} else {
		char words[LINE_MAX_DOS], where[256], shown[PATH_MAX_DOS + 4];
		strlcpy(words, text, sizeof(words));
		int argc = split_words(words, argv, ARGS_MAX_DOS, true);
		switch (argc ? find_program(argv[0], where, sizeof(where), shown, sizeof(shown))
		             : NOT_FOUND) {
		case NOT_FOUND:
			printf("Bad command or file name\n");
			rc = 1;
			break;
		case DOS_PROGRAM:
			printf("%s: ManiDOS runs batch files and ManiOS programs, not DOS programs (.COM, .EXE)\n",
			       shown);
			rc = 1;
			break;
		case BATCH:
			rc = run_batch(where, argv[0], argc, argv, batch_call_pending());
			break;
		case PROGRAM: {
			static char bufs[ARGS_MAX_DOS][256];
			char *pargv[ARGS_MAX_DOS + 1];
			const char *base = strrchr(where, '/');
			pargv[0] = (char *)(base ? base + 1 : where);
			for (int i = 1; i < argc; i++) {
				pargv[i] = translate(argv[i], bufs[i], sizeof(bufs[i]));
			}
			pargv[argc] = NULL;
			int pid = spawn_with(where, pargv, 0, 1);
			rc = pid < 0 ? (printf("%s: can't start it\n", shown), 1) : wait_for(pid, shown);
			break;
		}
		}
	}
	fflush(stdout);
	if (saved_in >= 0) {
		dup2(saved_in, 0);
		close(saved_in);
	}
	if (saved_out >= 0) {
		dup2(saved_out, 1);
		close(saved_out);
	}
	return rc;
}

/* A command in the middle of a pipeline, in a process of its own: a
 * ManiOS program directly, anything else in a second ManiDOS. */
static int run_apart(struct stage *s, int in, int out)
{
	char words[LINE_MAX_DOS], where[256], shown[PATH_MAX_DOS + 4], *argv[ARGS_MAX_DOS + 1], *rest;
	strlcpy(words, s->text, sizeof(words));
	int argc = split_words(words, argv, ARGS_MAX_DOS, true);
	char probe[LINE_MAX_DOS];
	strlcpy(probe, s->text, sizeof(probe));
	char *t = probe + strspn(probe, " \t");
	if (argc && !internal(t, &rest)
	    && find_program(argv[0], where, sizeof(where), shown, sizeof(shown)) == PROGRAM) {
		static char bufs[ARGS_MAX_DOS][256];
		char *pargv[ARGS_MAX_DOS + 1];
		const char *base = strrchr(where, '/');
		pargv[0] = (char *)(base ? base + 1 : where);
		for (int i = 1; i < argc; i++) {
			pargv[i] = translate(argv[i], bufs[i], sizeof(bufs[i]));
		}
		pargv[argc] = NULL;
		return spawn_with(where, pargv, in, out);
	}
	char *dargv[] = { "dos", "/C", s->text, NULL };
	return spawn_with("/bin/dos", dargv, in, out);
}

/* Cuts "< F", "> F" and ">> F" out of a stage's text. */
static int redirections(struct stage *s)
{
	static char names[MAX_STAGES * 2][PATH_MAX_DOS];
	static int used;
	char out[LINE_MAX_DOS];
	size_t o = 0;
	bool quoted = false;
	used = used >= MAX_STAGES * 2 - 2 ? 0 : used;
	for (char *p = s->text; *p;) {
		if (*p == '"') {
			quoted = !quoted;
		}
		if (quoted || (*p != '<' && *p != '>')) {
			out[o++] = *p++;
			continue;
		}
		bool output = *p == '>';
		bool append = output && p[1] == '>';
		p += append ? 2 : 1;
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		size_t n = strcspn(p, " \t<>|");
		if (!n) {
			printf("Syntax error\n");
			return -1;
		}
		char *name = names[used++];
		memcpy(name, p, n < PATH_MAX_DOS - 1 ? n : PATH_MAX_DOS - 1);
		name[n < PATH_MAX_DOS - 1 ? n : PATH_MAX_DOS - 1] = '\0';
		if (output) {
			s->out_file = name;
			s->append = append;
		} else {
			s->in_file = name;
		}
		p += n;
	}
	while (o && (out[o - 1] == ' ' || out[o - 1] == '\t')) {
		o--; /* "ECHO hi > CON" says "hi" */
	}
	out[o] = '\0';
	strlcpy(s->text, out, sizeof(s->text));
	return 0;
}

int run_line(const char *line)
{
	char expanded[LINE_MAX_DOS];
	expand(line, expanded, sizeof(expanded));
	char *p = expanded;
	while (*p == ' ' || *p == '\t' || *p == '@') {
		p++;
	}
	if (!*p) {
		return errorlevel;
	}
	/* A leading IF or FOR takes the rest of the line whole, pipes too. */
	struct stage stages[MAX_STAGES];
	int n = 0;
	bool quoted = false;
	char *start = p;
	char *rest;
	const struct command *first = internal(p, &rest);
	bool whole = first && (!strcmp(first->name, "IF") || !strcmp(first->name, "FOR")
	                       || !strcmp(first->name, "REM"));
	for (;; p++) {
		if (*p == '"') {
			quoted = !quoted;
		}
		if (*p == '\0' || (*p == '|' && !quoted && !whole)) {
			if (n == MAX_STAGES) {
				printf("Too many commands in the pipeline\n");
				return errorlevel = 1;
			}
			memset(&stages[n], 0, sizeof(stages[n]));
			size_t len = (size_t)(p - start);
			len = len < sizeof(stages[n].text) - 1 ? len : sizeof(stages[n].text) - 1;
			memcpy(stages[n].text, start, len);
			stages[n].text[len] = '\0';
			if (!whole && redirections(&stages[n]) < 0) {
				return errorlevel = 1;
			}
			if (!stages[n].text[strspn(stages[n].text, " \t")]) {
				printf("Syntax error\n");
				return errorlevel = 1;
			}
			n++;
			if (!*p) {
				break;
			}
			start = p + 1;
		}
	}

	int pids[MAX_STAGES], npids = 0, next_in = -1, rc = 0;
	for (int i = 0; i < n; i++) {
		struct stage *s = &stages[i];
		int in = next_in >= 0 ? next_in : 0, out = 1, pipe_fds[2] = { -1, -1 };
		next_in = -1;
		if (s->in_file) {
			if (in != 0) {
				close(in);
			}
			if ((in = open_redirect(s->in_file, false, false)) < 0) {
				rc = 1;
				break;
			}
		}
		if (s->out_file) {
			if ((out = open_redirect(s->out_file, true, s->append)) < 0) {
				if (in != 0) {
					close(in);
				}
				rc = 1;
				break;
			}
		} else if (i < n - 1) {
			if (pipe(pipe_fds) < 0) {
				printf("Can't make a pipe\n");
				rc = 1;
				break;
			}
			out = pipe_fds[0];
			next_in = pipe_fds[1];
		}
		if (i < n - 1) {
			int pid = run_apart(s, in, out);
			if (pid < 0) {
				printf("Bad command or file name\n");
			} else {
				pids[npids++] = pid;
			}
		} else {
			rc = run_here(s, in, out);
		}
		if (in != 0) {
			close(in);
		}
		if (out != 1) {
			close(out);
		}
	}
	if (next_in >= 0) {
		close(next_in);
	}
	for (int i = 0; i < npids; i++) {
		int status;
		wait(pids[i], &status);
	}
	return errorlevel = rc;
}

/* --- start-up --- */

static void banner(void)
{
	printf("\nManiDOS %s -- a disk operating system for ManiOS\n", DOS_VERSION);
	printf("Drives:");
	for (int d = 0; d < 26; d++) {
		if (!drives[d].present) {
			continue;
		}
		const char *what = drives[d].kind;
		char name[32];
		if (drives[d].device[0]) {
			snprintf(name, sizeof(name), "%s", drives[d].device + 5); /* after "/dev/" */
			what = name;
		}
		printf("  %c: %s", 'A' + d, what);
	}
	printf("\nType HELP for the commands, EXIT to go back to ManiOS.\n\n");
}

int main(int argc, char **argv)
{
	keyboard_fd = dup(0);
	terminal_fd = dup(1);
	drives_init();
	env_set("PATH", "Z:\\BIN");
	env_set("PROMPT", "$P$G");
	env_set("COMSPEC", "Z:\\BIN\\DOS");
	if (argc > 1 && !strcasecmp(argv[1], "/C")) {
		/* One command line, in the directory ManiOS started us in. */
		drives_follow_cwd();
		char line[LINE_MAX_DOS] = "";
		for (int i = 2; i < argc; i++) {
			if (i > 2) {
				strlcat(line, " ", sizeof(line));
			}
			strlcat(line, argv[i], sizeof(line));
		}
		run_line(line);
		fflush(stdout);
		return errorlevel;
	}
	if (argc > 1) {
		fprintf(stderr, "usage: dos [/C COMMAND]\n");
		return 2;
	}
	/* Started in a directory of a FAT drive or the boot disk, begin
	 * there; otherwise on C: (or A: without a FAT disk). */
	int first = current;
	drives_follow_cwd();
	if (current == 'Z' - 'A') {
		current = first;
		strcpy(drives['Z' - 'A'].cwd, "\\");
	}
	banner();
	char where[256];
	if (dos_manios("A:\\AUTOEXEC.BAT", where, sizeof(where)) == 0 && dos_type(where, NULL) == ZKT_TYPE_FILE) {
		char *args[] = { "A:\\AUTOEXEC.BAT", NULL };
		run_batch(where, args[0], 1, args, false);
	}
	struct line_reader r;
	char line[LINE_MAX_DOS];
	reader_init(&r, 0);
	while (!leaving) {
		if (echo_on) {
			print_prompt();
		}
		if (!reader_line(&r, line, sizeof(line))) {
			break; /* end of input */
		}
		strip_escapes(line);
		run_line(line);
		if (echo_on && !leaving) {
			putchar('\n');
		}
	}
	fflush(stdout);
	return errorlevel;
}
