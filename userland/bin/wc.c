/* wc [FILES...] -- count lines, words and bytes. */
#include <ctype.h>
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <string.h>

static unsigned long total[3];

static int count(int fd, const char *name)
{
	unsigned long lines = 0, words = 0, bytes = 0;
	int in_word = 0;
	char buf[1024];
	long n;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		bytes += (unsigned long)n;
		for (long i = 0; i < n; i++) {
			lines += buf[i] == '\n';
			if (isspace((unsigned char)buf[i])) {
				in_word = 0;
			} else if (!in_word) {
				in_word = 1;
				words++;
			}
		}
	}
	if (n < 0) {
		fprintf(stderr, "wc: %s: %s\n", name, strerror(errno));
		return 1;
	}
	printf("%7lu %7lu %7lu%s%s\n", lines, words, bytes, *name ? " " : "", name);
	total[0] += lines;
	total[1] += words;
	total[2] += bytes;
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		return count(0, "");
	}
	int rc = 0;
	for (int i = 1; i < argc; i++) {
		int fd = open(argv[i], OREAD);
		if (fd < 0) {
			fprintf(stderr, "wc: %s: %s\n", argv[i], strerror(errno));
			rc = 1;
			continue;
		}
		rc |= count(fd, argv[i]);
		close(fd);
	}
	if (argc > 2) {
		printf("%7lu %7lu %7lu total\n", total[0], total[1], total[2]);
	}
	return rc;
}
