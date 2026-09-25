#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

__attribute__((noreturn)) void __assert_fail(const char *expr, const char *file, int line)
{
	fflush(stdout);
	fprintf(stderr, "assertion failed: %s, %s:%d\n", expr, file, line);
	abort();
}
