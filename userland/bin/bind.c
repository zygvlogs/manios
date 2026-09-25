/* bind [-a|-b] NEW OLD -- bind NEW onto OLD in this namespace (shared
 * with the shell that ran it, ADR-0003): replace, or make a union with
 * NEW searched after (-a) or before (-b) OLD. */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	int flag = BIND_FLAG_REPLACE, i = 1;
	if (argc == 4 && !strcmp(argv[1], "-a")) {
		flag = BIND_FLAG_AFTER;
		i++;
	} else if (argc == 4 && !strcmp(argv[1], "-b")) {
		flag = BIND_FLAG_BEFORE;
		i++;
	} else if (argc != 3) {
		fprintf(stderr, "usage: bind [-a|-b] NEW OLD\n");
		return 1;
	}
	if (bind(argv[i], argv[i + 1], flag) < 0) {
		fprintf(stderr, "bind: %s\n", strerror(errno));
		return 1;
	}
	return 0;
}
