/* Apart from string.c, so that using strlen doesn't link malloc. */
#include <stdlib.h>
#include <string.h>

char *strndup(const char *s, size_t n)
{
	size_t len = 0;
	while (len < n && s[len]) {
		len++;
	}
	char *d = malloc(len + 1);
	if (d) {
		memcpy(d, s, len);
		d[len] = '\0';
	}
	return d;
}

char *strdup(const char *s)
{
	return strndup(s, (size_t)-1);
}
