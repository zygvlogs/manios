/* sh -- the ManiOS shell.
 *
 *   sh              read commands from standard input, with a prompt
 *   sh FILE         run the commands in FILE
 *   sh -c COMMAND   run one command
 *
 * Commands are separated by newlines or ';'; words by blanks. '...'
 * quotes literally, "..." quotes with \" and \\ escapes, \ escapes the
 * next character, and # at the start of a word begins a comment. A name
 * without '/' runs /bin/NAME; the program shares the shell's namespace,
 * so `bind` and `unbind` change the shell's view too (ADR-0003).
 * Builtins: cd, exit, newns, help. */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX 512
#define PROMPT "manios% "

static int status; /* of the last command: an exit code, or 128 + the vector that killed it */

static int blank(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Splits the first command of *line into words, in place, and points
 * *line at the next command (NULL when there is none). Returns the word
 * count, or -1 after reporting a syntax error. */
static int split(char **line, char **argv, int max)
{
	char *in = *line, *out = *line;
	int argc = 0;
	*line = NULL;
	for (;;) {
		while (blank(*in)) {
			in++;
		}
		if (!*in || *in == '#') {
			break;
		}
		if (*in == ';') {
			*line = in + 1;
			break;
		}
		if (argc == max) {
			fprintf(stderr, "sh: too many words\n");
			return -1;
		}
		argv[argc++] = out;
		while (*in && !blank(*in) && *in != ';') {
			if (*in == '\'' || *in == '"') {
				char quote = *in++;
				while (*in && *in != quote) {
					if (quote == '"' && *in == '\\' && (in[1] == '"' || in[1] == '\\')) {
						in++;
					}
					*out++ = *in++;
				}
				if (!*in) {
					fprintf(stderr, "sh: missing %c\n", quote);
					return -1;
				}
				in++;
			} else if (*in == '\\' && in[1]) {
				in++;
				*out++ = *in++;
			} else {
				*out++ = *in++;
			}
		}
		/* out never passes in, so read the separator before ending the word over it. */
		char separator = *in;
		*out++ = '\0';
		if (!separator) {
			break;
		}
		in++;
		if (separator == ';') {
			*line = in;
			break;
		}
	}
	argv[argc] = NULL;
	return argc;
}

static void builtin_help(void)
{
	printf("builtins: cd [DIR], exit [CODE], newns, help\n"
	       "other commands run from /bin (ls /bin), or by path\n");
}

/* Returns 1 if argv[0] was a builtin (and ran it). */
static int builtin(int argc, char **argv)
{
	if (!strcmp(argv[0], "cd")) {
		const char *dir = argc > 1 ? argv[1] : "/";
		status = 0;
		if (chdir(dir) < 0) {
			fprintf(stderr, "sh: cd: %s: %s\n", dir, strerror(errno));
			status = 1;
		}
	} else if (!strcmp(argv[0], "exit")) {
		exit(argc > 1 ? atoi(argv[1]) : status);
	} else if (!strcmp(argv[0], "newns")) {
		/* From now on, binds made here (and by commands run from here)
		 * stay private to this shell and its children. */
		status = nsfork() < 0;
		if (status) {
			perror("sh: newns");
		}
	} else if (!strcmp(argv[0], "help")) {
		builtin_help();
		status = 0;
	} else {
		return 0;
	}
	return 1;
}

static void execute(int argc, char **argv)
{
	if (builtin(argc, argv)) {
		return;
	}

	char path[ZKT_PATH_MAX + 1];
	if (strchr(argv[0], '/')) {
		strlcpy(path, argv[0], sizeof(path));
	} else {
		snprintf(path, sizeof(path), "/bin/%s", argv[0]);
	}
	fflush(stdout);
	int pid = spawn(path, argv);
	if (pid < 0) {
		fprintf(stderr, "sh: %s: %s\n", argv[0], errno == ENOENT ? "not found" : strerror(errno));
		status = 127;
		return;
	}
	int st;
	if (wait(pid, &st) < 0) {
		perror("sh: wait");
		status = 1;
	} else if (st & ZKT_WAIT_KILLED) {
		fprintf(stderr, "sh: %s: killed (vector %d)\n", argv[0], ZKT_WAIT_VECTOR(st));
		status = 128 + ZKT_WAIT_VECTOR(st);
	} else {
		status = ZKT_WAIT_CODE(st);
	}
}

static void run(char *line)
{
	while (line) {
		char *argv[ZKT_ARGS_MAX + 1];
		int argc = split(&line, argv, ZKT_ARGS_MAX);
		if (argc < 0) {
			status = 2;
			return;
		}
		if (argc > 0) {
			execute(argc, argv);
		}
	}
}

int main(int argc, char **argv)
{
	char line[LINE_MAX];
	if (argc == 3 && !strcmp(argv[1], "-c")) {
		strlcpy(line, argv[2], sizeof(line));
		run(line);
		return status;
	}
	if (argc > 2) {
		fprintf(stderr, "usage: sh [FILE | -c COMMAND]\n");
		return 2;
	}
	FILE *in = stdin;
	if (argc == 2 && !(in = fopen(argv[1], "r"))) {
		fprintf(stderr, "sh: %s: %s\n", argv[1], strerror(errno));
		return 127;
	}
	for (;;) {
		if (in == stdin) {
			fputs(PROMPT, stdout);
		}
		if (!fgets(line, sizeof(line), in)) {
			break;
		}
		run(line);
	}
	if (in == stdin) {
		putchar('\n'); /* end of input left the cursor after the prompt */
	}
	return status;
}
