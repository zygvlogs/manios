/* cat [FILES...] -- copy files (or standard input) to standard output. */
#include <manios.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int copy(int fd, const char *name)
{
	char buf[1024];
	long n;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		for (long done = 0; done < n;) {
			long w = write(1, buf + done, (size_t)(n - done));
			if (w < 0 && errno == EPIPE) {
				_exit(141); /* the reader has gone: quietly, as stdio ends a program */
			}
			if (w <= 0) {
				perror("cat: write");
				return 1;
			}
			done += w;
		}
	}
	if (n < 0) {
		fprintf(stderr, "cat: %s: %s\n", name, strerror(errno));
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		return copy(0, "standard input");
	}
	int rc = 0;
	for (int i = 1; i < argc; i++) {
		int fd = open(argv[i], OREAD);
		if (fd < 0) {
			fprintf(stderr, "cat: %s: %s\n", argv[i], strerror(errno));
			rc = 1;
			continue;
		}
		rc |= copy(fd, argv[i]);
		close(fd);
	}
	return rc;
}
