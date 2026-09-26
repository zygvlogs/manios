/* getopt(), as POSIX describes it: options until the first operand or
 * "--"; grouped flags (-ab); an argument joined (-n5) or next (-n 5).
 * A leading ':' in optstring reports a missing argument as ':' and
 * prints nothing. optind = 0 starts over. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *optarg;
int optind = 1, opterr = 1, optopt;

static int pos = 1; /* where in argv[optind] the next flag is */

static void next_flag(char *const argv[])
{
	if (!argv[optind][++pos]) {
		optind++;
		pos = 1;
	}
}

int getopt(int argc, char *const argv[], const char *optstring)
{
	bool quiet = optstring[0] == ':';
	optarg = NULL;
	if (optind == 0) {
		optind = 1;
		pos = 1;
	}
	if (optind >= argc || !argv[optind] || argv[optind][0] != '-' || !argv[optind][1]) {
		return -1;
	}
	if (pos == 1 && !strcmp(argv[optind], "--")) {
		optind++;
		return -1;
	}
	int c = (unsigned char)argv[optind][pos];
	const char *spec = c == ':' ? NULL : strchr(optstring + quiet, c);
	if (!spec) {
		optopt = c;
		if (opterr && !quiet) {
			fprintf(stderr, "%s: unknown option -- %c\n", getprogname(), c);
		}
		next_flag(argv);
		return '?';
	}
	if (spec[1] != ':') {
		next_flag(argv);
		return c;
	}
	if (argv[optind][pos + 1]) {
		optarg = &argv[optind][pos + 1];
		optind++;
	} else if (optind + 1 < argc) {
		optarg = argv[optind + 1];
		optind += 2;
	} else {
		optopt = c;
		optind++;
		pos = 1;
		if (quiet) {
			return ':';
		}
		if (opterr) {
			fprintf(stderr, "%s: option requires an argument -- %c\n", getprogname(), c);
		}
		return '?';
	}
	pos = 1;
	return c;
}
