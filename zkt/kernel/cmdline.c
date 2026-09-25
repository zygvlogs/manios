#include "cmdline.h"
#include "kstring.h"

static char line[CMDLINE_MAX];

void cmdline_set(const char *s)
{
	strlcpy(line, s, sizeof(line));
}

const char *cmdline(void)
{
	return line;
}

bool cmdline_get(const char *key, char *out, size_t size)
{
	size_t key_len = strlen(key);
	for (const char *w = line; *w;) {
		while (*w == ' ') {
			w++;
		}
		const char *end = w;
		while (*end && *end != ' ') {
			end++;
		}
		if ((size_t)(end - w) > key_len && !strncmp(w, key, key_len) && w[key_len] == '=') {
			size_t n = (size_t)(end - w) - key_len - 1;
			if (n >= size) {
				return false;
			}
			memcpy(out, w + key_len + 1, n);
			out[n] = '\0';
			return true;
		}
		w = end;
	}
	return false;
}
