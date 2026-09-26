/* The program's name, for messages (err.c, getopt.c): the last part of
 * argv[0]. crt0.S calls __libc_init() before main(). BSD programs name
 * it __progname. */
#include <stdlib.h>

char *__progname = "";

void __libc_init(int argc, char **argv);

void __libc_init(int argc, char **argv)
{
	if (argc > 0 && argv[0]) {
		__progname = argv[0];
		for (char *p = argv[0]; *p; p++) {
			if (*p == '/' && p[1]) {
				__progname = p + 1;
			}
		}
	}
}

const char *getprogname(void)
{
	return __progname;
}
