/* unbind OLD -- undo every bind on OLD in this namespace. */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: unbind OLD\n");
		return 1;
	}
	if (unbind(argv[1]) < 0) {
		fprintf(stderr, "unbind: %s: %s\n", argv[1], strerror(errno));
		return 1;
	}
	return 0;
}
