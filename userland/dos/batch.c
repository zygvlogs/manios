/* Batch files (dos.h): a .BAT file's lines run one by one, with %0-%9
 * its parameters, %NAME% the environment's variables, @ and ECHO OFF to
 * keep lines from being shown, :LABELs for GOTO, and IF, FOR, CALL and
 * SHIFT. A batch file started from another without CALL takes its place
 * (the first ends); with CALL, the first carries on afterwards. */
#include "dos.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NESTING 8
#define MAX_PARAMS 32

struct batch {
	char *text;
	char **lines;
	int count, next;
	char *params[MAX_PARAMS];
	int nparams, shift;
	bool done;
};

static struct batch *stack[MAX_NESTING];
static int depth;
static bool calling;

bool in_batch(void)
{
	return depth > 0;
}

static struct batch *top(void)
{
	return depth ? stack[depth - 1] : NULL;
}

/* %VAR% if VAR is a defined name; *used is how much of `in` it took. */
static const char *variable(const char *in, size_t *used)
{
	const char *end = strchr(in + 1, '%');
	if (!end || end == in + 1 || end - in > 64) {
		return NULL;
	}
	char name[65];
	memcpy(name, in + 1, (size_t)(end - in - 1));
	name[end - in - 1] = '\0';
	if (strpbrk(name, " \t")) {
		return NULL;
	}
	const char *v = env_get(name);
	if (v) {
		*used = (size_t)(end - in) + 1;
	}
	return v;
}

void expand(const char *in, char *out, size_t size)
{
	struct batch *b = top();
	size_t o = 0;
	for (const char *p = in; *p && o + 1 < size;) {
		const char *add = NULL;
		size_t used = 0;
		if (*p == '%' && b && p[1] == '%') {
			add = "%"; /* %% is a %, as FOR's %%V needs */
			used = 2;
		} else if (*p == '%' && b && isdigit((unsigned char)p[1])) {
			int i = p[1] - '0' + (p[1] == '0' ? 0 : b->shift);
			add = i < b->nparams ? b->params[i] : "";
			used = 2;
		} else if (*p == '%') {
			add = variable(p, &used);
		}
		if (add) {
			for (const char *a = add; *a && o + 1 < size; a++) {
				out[o++] = *a;
			}
			p += used;
		} else {
			out[o++] = *p++;
		}
	}
	out[o] = '\0';
}

static int load(const char *manios_path, struct batch *b)
{
	int fd = open(manios_path, OREAD);
	if (fd < 0) {
		return -1;
	}
	size_t len = 0, cap = 4096;
	b->text = malloc(cap + 1);
	long n;
	while (b->text && (n = read(fd, b->text + len, cap - len)) > 0) {
		len += (size_t)n;
		if (len == cap) {
			cap *= 2;
			char *grown = realloc(b->text, cap + 1);
			if (!grown) {
				free(b->text);
				b->text = NULL;
			}
			b->text = grown;
		}
	}
	close(fd);
	if (!b->text) {
		return -1;
	}
	b->text[len] = '\0';
	/* Lines, with DOS's CR LF or ManiOS's LF; ^Z ends the text. */
	char *eof = strchr(b->text, 0x1A);
	if (eof) {
		*eof = '\0';
	}
	int lines = 1;
	for (char *p = b->text; *p; p++) {
		lines += *p == '\n';
	}
	b->lines = malloc((size_t)lines * sizeof(*b->lines));
	if (!b->lines) {
		return -1;
	}
	b->count = 0;
	for (char *line = b->text; line;) {
		char *nl = strchr(line, '\n');
		if (nl) {
			*nl = '\0';
		}
		size_t l = strlen(line);
		if (l && line[l - 1] == '\r') {
			line[l - 1] = '\0';
		}
		b->lines[b->count++] = line;
		line = nl ? nl + 1 : NULL;
	}
	return 0;
}

int run_batch(const char *manios_path, const char *display, int argc, char **argv, bool call)
{
	if (depth == MAX_NESTING) {
		printf("Batch files are nested too deeply\n");
		return 1;
	}
	/* Without CALL, a batch file run from another replaces it. */
	if (!call && depth) {
		top()->done = true;
	}
	struct batch *b = calloc(1, sizeof(*b));
	if (!b || load(manios_path, b) < 0) {
		free(b);
		printf("Batch file missing\n");
		return 1;
	}
	b->params[b->nparams++] = strdup(display);
	for (int i = 1; i < argc && b->nparams < MAX_PARAMS; i++) {
		b->params[b->nparams++] = strdup(argv[i]);
	}
	bool echo_before = echo_on;
	stack[depth++] = b;
	while (b->next < b->count && !b->done && !leaving) {
		char *line = b->lines[b->next++];
		const char *p = line;
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		if (*p == ':' || !*p) {
			continue; /* a label, or nothing */
		}
		if (echo_on && *p != '@') {
			print_prompt();
			printf("%s\n", p);
		}
		run_line(p);
	}
	depth--;
	for (int i = 0; i < b->nparams; i++) {
		free(b->params[i]);
	}
	free(b->lines);
	free(b->text);
	free(b);
	if (!depth) {
		echo_on = echo_before; /* back at the prompt, as it was */
	}
	return errorlevel;
}

void batch_goto(const char *label)
{
	struct batch *b = top();
	if (!b) {
		return;
	}
	const char *want = label[0] == ':' ? label + 1 : label;
	if (!strcasecmp(want, "EOF")) {
		b->done = true;
		return;
	}
	for (int i = 0; i < b->count; i++) {
		const char *l = b->lines[i];
		while (*l == ' ' || *l == '\t') {
			l++;
		}
		if (*l != ':') {
			continue;
		}
		size_t n = strcspn(l + 1, " \t");
		if (n == strlen(want) && !strncasecmp(l + 1, want, n)) {
			b->next = i + 1;
			return;
		}
	}
	printf("Label not found\n");
	b->done = true;
}

void batch_shift(void)
{
	if (top()) {
		top()->shift++;
	}
}

int run_call(const char *line)
{
	calling = true;
	int rc = run_line(line);
	calling = false;
	return rc;
}

bool batch_call_pending(void)
{
	bool c = calling;
	calling = false;
	return c;
}

/* --- IF --- */

static const char *skip(const char *s)
{
	while (*s == ' ' || *s == '\t') {
		s++;
	}
	return s;
}

/* The next word (quotes kept) into out; returns what follows. */
static const char *next_word(const char *s, char *out, size_t size)
{
	size_t n = 0;
	s = skip(s);
	bool quoted = false;
	for (; *s && (quoted || (*s != ' ' && *s != '\t')); s++) {
		if (*s == '"') {
			quoted = !quoted;
		}
		if (n + 1 < size) {
			out[n++] = *s;
		}
	}
	out[n] = '\0';
	return s;
}

int if_command(const char *rest)
{
	char word[LINE_MAX_DOS];
	const char *p = next_word(rest, word, sizeof(word));
	bool negate = !strcasecmp(word, "NOT");
	if (negate) {
		p = next_word(p, word, sizeof(word));
	}
	bool yes;
	if (!strcasecmp(word, "ERRORLEVEL")) {
		p = next_word(p, word, sizeof(word));
		yes = errorlevel >= atoi(word);
	} else if (!strcasecmp(word, "EXIST")) {
		char where[256];
		p = next_word(p, word, sizeof(word));
		if (has_wild(word)) {
			char dir[PATH_MAX_DOS], pattern[64];
			split_last(word, dir, sizeof(dir), pattern, sizeof(pattern));
			struct { const char *pattern; bool found; } m = { pattern, false };
			if (dos_manios(dir[0] ? dir : ".", where, sizeof(where)) == 0) {
				int fd = open(where, OREAD);
				struct zkt_dirent e;
				while (fd >= 0 && !m.found && read(fd, &e, sizeof(e)) == sizeof(e)) {
					m.found = wild_match(m.pattern, e.name);
				}
				if (fd >= 0) {
					close(fd);
				}
			}
			yes = m.found;
		} else {
			yes = dos_manios(word, where, sizeof(where)) == 0 && dos_type(where, NULL) >= 0;
		}
	} else {
		/* A==B: the words either side of ==, compared as they are. */
		char *eq = strstr(word, "==");
		char left[LINE_MAX_DOS], right[LINE_MAX_DOS];
		if (!eq) {
			/* "A == B" with spaces */
			strlcpy(left, word, sizeof(left));
			p = next_word(p, word, sizeof(word));
			if (strncmp(word, "==", 2)) {
				printf("Syntax error\n");
				return 1;
			}
			if (word[2]) {
				strlcpy(right, word + 2, sizeof(right));
			} else {
				p = next_word(p, right, sizeof(right));
			}
		} else {
			*eq = '\0';
			strlcpy(left, word, sizeof(left));
			if (eq[2]) {
				strlcpy(right, eq + 2, sizeof(right));
			} else {
				p = next_word(p, right, sizeof(right));
			}
		}
		yes = !strcmp(left, right);
	}
	p = skip(p);
	if (!*p) {
		printf("Syntax error\n");
		return 1;
	}
	return yes != negate ? run_line(p) : 0;
}

/* --- FOR --- */

struct for_items {
	char **items;
	int n, cap;
	char prefix[PATH_MAX_DOS];
	const char *pattern;
};

static void add_item(struct for_items *f, const char *item)
{
	if (f->n == f->cap) {
		f->cap = f->cap ? f->cap * 2 : 16;
		char **grown = realloc(f->items, (size_t)f->cap * sizeof(*grown));
		if (!grown) {
			return;
		}
		f->items = grown;
	}
	f->items[f->n++] = strdup(item);
}

static void add_match(const struct zkt_dirent *e, void *arg)
{
	struct for_items *f = arg;
	if (e->type != ZKT_TYPE_DIR && wild_match(f->pattern, e->name)) {
		char item[PATH_MAX_DOS + ZKT_NAME_MAX + 2];
		snprintf(item, sizeof(item), "%s%s", f->prefix, e->name);
		add_item(f, upper(item));
	}
}

/* Replaces each %V in cmd (V the variable's letter) with value. */
static void substitute(const char *cmd, char var, const char *value, char *out, size_t size)
{
	size_t o = 0;
	for (const char *p = cmd; *p && o + 1 < size;) {
		if (p[0] == '%' && toupper((unsigned char)p[1]) == toupper((unsigned char)var)) {
			for (const char *v = value; *v && o + 1 < size; v++) {
				out[o++] = *v;
			}
			p += 2;
		} else {
			out[o++] = *p++;
		}
	}
	out[o] = '\0';
}

int for_command(const char *rest)
{
	/* %V IN (SET) DO COMMAND -- in a batch file %%V, made %V already. */
	const char *p = skip(rest);
	if (p[0] != '%' || !isalpha((unsigned char)p[1])) {
		printf("Syntax error\n");
		return 1;
	}
	char var = p[1];
	p = skip(p + 2);
	if (strncasecmp(p, "IN", 2)) {
		printf("Syntax error\n");
		return 1;
	}
	p = skip(p + 2);
	const char *close = *p == '(' ? strchr(p, ')') : NULL;
	if (!close) {
		printf("Syntax error\n");
		return 1;
	}
	char set[LINE_MAX_DOS];
	size_t n = (size_t)(close - p - 1) < sizeof(set) - 1 ? (size_t)(close - p - 1) : sizeof(set) - 1;
	memcpy(set, p + 1, n);
	set[n] = '\0';
	p = skip(close + 1);
	if (strncasecmp(p, "DO", 2) || (p[2] != ' ' && p[2] != '\t')) {
		printf("Syntax error\n");
		return 1;
	}
	const char *cmd = skip(p + 2);

	struct for_items f = { 0 };
	char *save;
	for (char *item = strtok_r(set, " \t,;", &save); item; item = strtok_r(NULL, " \t,;", &save)) {
		if (!has_wild(item)) {
			add_item(&f, item);
			continue;
		}
		char dir[PATH_MAX_DOS], pattern[64], where[256];
		split_last(item, dir, sizeof(dir), pattern, sizeof(pattern));
		f.pattern = pattern;
		snprintf(f.prefix, sizeof(f.prefix), "%s%s", dir,
		         dir[0] && !strchr("\\/:", dir[strlen(dir) - 1]) ? "\\" : "");
		if (dos_manios(dir[0] ? dir : ".", where, sizeof(where)) == 0) {
			each_entry(where, add_match, &f);
		}
	}
	int rc = 0;
	for (int i = 0; i < f.n && !leaving; i++) {
		char line[LINE_MAX_DOS];
		substitute(cmd, var, f.items[i], line, sizeof(line));
		if (echo_on && in_batch()) {
			print_prompt();
			printf("%s\n", line);
		}
		rc = run_line(line);
	}
	for (int i = 0; i < f.n; i++) {
		free(f.items[i]);
	}
	free(f.items);
	return rc;
}
