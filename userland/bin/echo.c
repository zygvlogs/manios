/* echo [-n] [ARGS...] -- print the arguments; -n: no newline. */
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	int i = 1;
	int newline = 1;
	if (argc > 1 && !strcmp(argv[1], "-n")) {
		newline = 0;
		i++;
	}
	for (int first = i; i < argc; i++) {
		printf(i > first ? " %s" : "%s", argv[i]);
	}
	if (newline) {
		putchar('\n');
	}
	return 0;
}
