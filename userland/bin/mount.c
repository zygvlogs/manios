/* mount [-a|-b] DIAL OLD [ANAME] -- attach to the ZRP server at DIAL
 * (udp!A.B.C.D[!PORT], docs/zrp.md) and bind its tree onto OLD in this
 * namespace: replacing it, or as a union searched after (-a) or before
 * (-b) what is there. */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	int flag = BIND_FLAG_REPLACE, i = 1;
	if (argc > 1 && !strcmp(argv[1], "-a")) {
		flag = BIND_FLAG_AFTER;
		i++;
	} else if (argc > 1 && !strcmp(argv[1], "-b")) {
		flag = BIND_FLAG_BEFORE;
		i++;
	}
	if (argc - i < 2 || argc - i > 3) {
		fprintf(stderr, "usage: mount [-a|-b] DIAL OLD [ANAME]\n");
		return 1;
	}
	if (mount(argv[i], argv[i + 1], flag, argc - i == 3 ? argv[i + 2] : NULL) < 0) {
		fprintf(stderr, "mount: %s: %s\n", argv[i], strerror(errno));
		return 1;
	}
	return 0;
}
