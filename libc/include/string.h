#ifndef MANIOS_STRING_H
#define MANIOS_STRING_H

#include <stddef.h>

void *memset(void *dst, int c, size_t n);
void *memcpy(void *restrict dst, const void *restrict src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *restrict dst, const char *restrict src);
char *strcat(char *restrict dst, const char *restrict src);
char *strncpy(char *restrict dst, const char *restrict src, size_t n);
char *strncat(char *restrict dst, const char *restrict src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char *strpbrk(const char *s, const char *accept);
char *strtok(char *restrict s, const char *restrict delim);
char *strtok_r(char *restrict s, const char *restrict delim, char **restrict save);
void *memchr(const void *s, int c, size_t n);
char *strdup(const char *s);            /* malloc()ed; NULL when out of memory */
char *strndup(const char *s, size_t n);

/* The message for an error number (zkt_abi.h). */
char *strerror(int err);
int strcoll(const char *a, const char *b); /* strcmp(): the "C" locale */
/* The next token of *s, up to any byte in delim (made a NUL); *s moves
 * past it, or becomes NULL after the last. (BSD) */
char *strsep(char **s, const char *delim);
#include <strings.h> /* strcasecmp(): BSD programs expect it here too */

/* Copies at most size-1 bytes and always NUL-terminates (when size > 0).
 * Returns strlen(src), so truncation is `result >= size`. */
size_t strlcpy(char *restrict dst, const char *restrict src, size_t size);

#endif
