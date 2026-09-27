/* beep [FREQUENCY MILLISECONDS]... -- sounds the PC speaker (/dev/beep):
 * each pair is a note (a frequency of 0 is a rest); without any, one
 * beep of 440 Hz for 200 ms. */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int number(const char *s, unsigned long *out)
{
	char *end;
	errno = 0;
	*out = strtoul(s, &end, 10);
	return *s && !*end && !errno && *out <= 100000;
}

int main(int argc, char **argv)
{
	if (argc % 2 == 0) {
		fprintf(stderr, "usage: beep [FREQUENCY MILLISECONDS]...\n");
		return 1;
	}
	int fd = open("/dev/beep", OWRITE);
	if (fd < 0) {
		fprintf(stderr, "beep: /dev/beep: %s\n", strerror(errno));
		return 1;
	}
	char text[1024] = "";
	size_t n = 0;
	if (argc == 1) {
		n = (size_t)snprintf(text, sizeof(text), "440 200\n");
	}
	for (int i = 1; i + 1 < argc; i += 2) {
		unsigned long hz, ms;
		if (!number(argv[i], &hz) || !number(argv[i + 1], &ms)) {
			fprintf(stderr, "beep: not a frequency and a length: %s %s\n", argv[i], argv[i + 1]);
			return 1;
		}
		n += (size_t)snprintf(text + n, sizeof(text) - n, "%lu %lu\n", hz, ms);
		if (n >= sizeof(text) - 32) {
			fprintf(stderr, "beep: too many notes\n");
			return 1;
		}
	}
	if (write(fd, text, n) != (long)n) {
		fprintf(stderr, "beep: /dev/beep: %s\n", strerror(errno));
		return 1;
	}
	return 0;
}
