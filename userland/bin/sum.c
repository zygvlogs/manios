/* sum [FILES...] -- size and 32-bit FNV-1a hash of each file (the same
 * figures the kernel monitor's sum prints). */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <string.h>

static int sum(int fd, const char *name)
{
	uint32_t hash = 2166136261u;
	unsigned long size = 0;
	unsigned char buf[1024];
	long n;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		for (long i = 0; i < n; i++) {
			hash = (hash ^ buf[i]) * 16777619u;
		}
		size += (unsigned long)n;
	}
	if (n < 0) {
		fprintf(stderr, "sum: %s: %s\n", name, strerror(errno));
		return 1;
	}
	printf("%s: %lu bytes, fnv1a %08lx\n", name, size, (unsigned long)hash);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		return sum(0, "standard input");
	}
	int rc = 0;
	for (int i = 1; i < argc; i++) {
		int fd = open(argv[i], OREAD);
		if (fd < 0) {
			fprintf(stderr, "sum: %s: %s\n", argv[i], strerror(errno));
			rc = 1;
			continue;
		}
		rc |= sum(fd, argv[i]);
		close(fd);
	}
	return rc;
}
