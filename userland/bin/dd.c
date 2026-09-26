/* dd [if=FILE] [of=FILE] [bs=N] [count=N] [skip=N] [seek=N] -- copy
 * blocks of bs bytes (512 by default) from one file to another: standard
 * input and output unless named. skip= blocks of the input are passed
 * over, the output starts seek= blocks in, and count= limits how many
 * blocks are copied. With disks (/dev/ata0) that is how raw images are
 * written and read. Reports "N+M records in/out" as Unix dd does. */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BS_MAX (64 * 1024)

static long number(const char *s, const char *what)
{
	char *end;
	long v = strtol(s, &end, 10);
	if (*s == '\0' || *end || v < 0) {
		fprintf(stderr, "dd: bad %s: %s\n", what, s);
		exit(2);
	}
	return v;
}

int main(int argc, char **argv)
{
	const char *in_path = 0, *out_path = 0;
	long bs = 512, count = -1, skip = 0, seek = 0;
	for (int i = 1; i < argc; i++) {
		char *eq = strchr(argv[i], '=');
		if (!eq) {
			fprintf(stderr, "usage: dd [if=FILE] [of=FILE] [bs=N] [count=N] [skip=N] [seek=N]\n");
			return 2;
		}
		*eq = '\0';
		const char *key = argv[i], *value = eq + 1;
		if (!strcmp(key, "if")) {
			in_path = value;
		} else if (!strcmp(key, "of")) {
			out_path = value;
		} else if (!strcmp(key, "bs")) {
			bs = number(value, "bs");
		} else if (!strcmp(key, "count")) {
			count = number(value, "count");
		} else if (!strcmp(key, "skip")) {
			skip = number(value, "skip");
		} else if (!strcmp(key, "seek")) {
			seek = number(value, "seek");
		} else {
			fprintf(stderr, "dd: unknown operand %s\n", key);
			return 2;
		}
	}
	if (bs < 1 || bs > BS_MAX) {
		fprintf(stderr, "dd: bs must be 1 to %d\n", BS_MAX);
		return 2;
	}
	int in = in_path ? open(in_path, OREAD) : 0;
	int out = out_path ? open(out_path, OWRITE) : 1;
	if (in < 0 || out < 0) {
		fprintf(stderr, "dd: %s: %s\n", in < 0 ? in_path : out_path, strerror(errno));
		return 1;
	}
	if ((skip && lseek(in, skip * bs, SEEK_SET) < 0) || (seek && lseek(out, seek * bs, SEEK_SET) < 0)) {
		fprintf(stderr, "dd: cannot seek: %s\n", strerror(errno));
		return 1;
	}
	char *buf = malloc((size_t)bs);
	if (!buf) {
		fprintf(stderr, "dd: out of memory\n");
		return 1;
	}
	long full_in = 0, part_in = 0, full_out = 0, part_out = 0;
	int rc = 0;
	while (count < 0 || full_in + part_in < count) {
		long got = 0, n;
		while (got < bs && (n = read(in, buf + got, (size_t)(bs - got))) > 0) {
			got += n;
		}
		if (got == 0) {
			if (n < 0) {
				fprintf(stderr, "dd: read: %s\n", strerror(errno));
				rc = 1;
			}
			break;
		}
		*(got == bs ? &full_in : &part_in) += 1;
		long put = 0;
		while (put < got && (n = write(out, buf + put, (size_t)(got - put))) > 0) {
			put += n;
		}
		if (put < got && n < 0 && errno == EPIPE) {
			_exit(141); /* the reader has gone: quietly, as stdio ends a program */
		}
		if (put < got) {
			fprintf(stderr, "dd: write: %s\n", put ? "short write" : strerror(errno));
			rc = 1;
			break;
		}
		*(put == bs ? &full_out : &part_out) += 1;
		if (got < bs) {
			break;
		}
	}
	fprintf(stderr, "%ld+%ld records in\n%ld+%ld records out\n", full_in, part_in, full_out,
	        part_out);
	return rc;
}
