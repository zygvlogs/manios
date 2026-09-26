/* getline() and getdelim(): a whole line, however long. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

ssize_t getdelim(char **line, size_t *size, int delim, FILE *f)
{
	if (!line || !size) {
		errno = EINVAL;
		return -1;
	}
	if (!*line) {
		*size = 0;
	}
	size_t len = 0;
	for (;;) {
		int c = getc(f);
		if (c == EOF) {
			break;
		}
		if (len + 2 > *size) {
			size_t grown = *size ? *size : 128;
			while (grown < len + 2) {
				grown *= 2;
			}
			char *p = realloc(*line, grown);
			if (!p) {
				errno = ENOMEM;
				return -1;
			}
			*line = p;
			*size = grown;
		}
		(*line)[len++] = (char)c;
		if (c == delim) {
			break;
		}
	}
	if (!len) {
		return -1; /* end of file (or an error: ferror() says) */
	}
	(*line)[len] = '\0';
	return (ssize_t)len;
}

ssize_t getline(char **line, size_t *size, FILE *f)
{
	return getdelim(line, size, '\n', f);
}
