#ifndef MANIOS_STDIO_H
#define MANIOS_STDIO_H

#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)
#define BUFSIZ 512
#define SEEK_SET 0 /* as in zkt_abi.h */
#define SEEK_CUR 1
#define SEEK_END 2

/* A buffered stream over a descriptor. stdin is read a buffer at a time
 * (from the console: a line at a time), stdout is flushed at every
 * newline, stderr is unbuffered. Reading from stdin flushes stdout
 * first, so prompts appear. */
typedef struct manios_file FILE;
extern FILE *stdin, *stdout, *stderr;

/* Modes "r", "w", "r+" (the kernel has no creating or truncating opens:
 * "w" opens an existing file or device for writing). */
FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *f);
int fflush(FILE *f); /* NULL: every stream */
int fileno(FILE *f);
int feof(FILE *f);
int ferror(FILE *f);
void clearerr(FILE *f);

/* whence: SEEK_SET, SEEK_CUR, SEEK_END. */
int fseek(FILE *f, long offset, int whence);
long ftell(FILE *f);
void rewind(FILE *f);

int fgetc(FILE *f);
int getc(FILE *f);
int getchar(void);
int ungetc(int c, FILE *f); /* one character */
char *fgets(char *s, int size, FILE *f);
size_t fread(void *buf, size_t size, size_t count, FILE *f);

int fputc(int c, FILE *f);
int putc(int c, FILE *f);
int putchar(int c);
int fputs(const char *s, FILE *f);
int puts(const char *s);
size_t fwrite(const void *buf, size_t size, size_t count, FILE *f);

/* Conversions: d i u x X o c s p %, with flags - 0 + space #, width
 * and precision (also as *), and the length modifiers hh h l ll z t j.
 * No floating point. */
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int fprintf(FILE *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int sprintf(char *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char *s, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int vsprintf(char *s, const char *fmt, va_list ap);
int vsnprintf(char *s, size_t size, const char *fmt, va_list ap);

/* "prefix: message for errno" on stderr. */
void perror(const char *prefix);

#endif
