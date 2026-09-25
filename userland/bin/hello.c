/* The first ManiOS user program: a greeting, then its arguments. */
#include <manios.h>
#include <string.h>

static void put(const char *s)
{
	write(1, s, strlen(s));
}

int main(int argc, char **argv)
{
	put("Hello from ManiOS userspace!\n");
	if (argc > 1) {
		put("args:");
		for (int i = 1; i < argc; i++) {
			put(" ");
			put(argv[i]);
		}
		put("\n");
	}
	return 0;
}
