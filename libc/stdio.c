#include <stdio.h>
#include <errno.h>
#include <manios.h>
#include <stdlib.h>
#include <string.h>
#include "format.h"

#define F_READ   1u
#define F_WRITE  2u
#define F_EOF    4u
#define F_ERR    8u
#define F_LINE   16u /* flush output at each newline */
#define F_UNBUF  32u
#define F_ALLOC  64u /* the FILE came from malloc() */

struct manios_file {
	int fd;
	unsigned flags;
	unsigned char *buf;
	size_t size;
	size_t rpos, rlen; /* buffered input: buf[rpos..rlen) */
	size_t wlen;       /* buffered output: buf[0..wlen) */
	int unget;         /* a pushed-back character, or EOF */
	struct manios_file *next;
};

static unsigned char in_buf[BUFSIZ], out_buf[BUFSIZ];
static struct manios_file std_in = { 0, F_READ, in_buf, BUFSIZ, 0, 0, 0, EOF, 0 };
static struct manios_file std_out = { 1, F_WRITE | F_LINE, out_buf, BUFSIZ, 0, 0, 0, EOF, 0 };
static struct manios_file std_err = { 2, F_WRITE | F_UNBUF, 0, 0, 0, 0, 0, EOF, 0 };
FILE *stdin = &std_in, *stdout = &std_out, *stderr = &std_err;

/* Streams fopen() made, for fflush(NULL). */
static struct manios_file *opened;

static int flush_out(FILE *f)
{
	size_t done = 0;
	while (done < f->wlen) {
		long n = write(f->fd, f->buf + done, f->wlen - done);
		if (n <= 0) {
			f->flags |= F_ERR;
			f->wlen = 0;
			return EOF;
		}
		done += (size_t)n;
	}
	f->wlen = 0;
	return 0;
}

int fflush(FILE *f)
{
	if (f) {
		return (f->flags & F_WRITE) ? flush_out(f) : 0;
	}
	int rc = fflush(stdout) | fflush(stderr);
	for (FILE *g = opened; g; g = g->next) {
		rc |= fflush(g);
	}
	return rc;
}

static FILE *make_file(int fd, unsigned mode)
{
	FILE *f = malloc(sizeof(*f) + BUFSIZ);
	if (!f) {
		return NULL;
	}
	memset(f, 0, sizeof(*f));
	f->fd = fd;
	f->flags = mode | F_ALLOC;
	f->buf = (unsigned char *)(f + 1);
	f->size = BUFSIZ;
	f->unget = EOF;
	f->next = opened;
	opened = f;
	return f;
}

static int parse_mode(const char *mode, unsigned *flags)
{
	if (!strcmp(mode, "r") || !strcmp(mode, "rb")) {
		*flags = F_READ;
		return OREAD;
	}
	if (!strcmp(mode, "w") || !strcmp(mode, "wb") || !strcmp(mode, "a")) {
		*flags = F_WRITE;
		return OWRITE;
	}
	if (!strcmp(mode, "r+") || !strcmp(mode, "w+") || !strcmp(mode, "rb+")) {
		*flags = F_READ | F_WRITE;
		return ORDWR;
	}
	return -1;
}

FILE *fopen(const char *path, const char *mode)
{
	unsigned flags;
	int omode = parse_mode(mode, &flags);
	if (omode < 0) {
		errno = EINVAL;
		return NULL;
	}
	int fd = open(path, omode);
	if (fd < 0) {
		return NULL;
	}
	FILE *f = make_file(fd, flags);
	if (!f) {
		close(fd);
	}
	return f;
}

FILE *fdopen(int fd, const char *mode)
{
	unsigned flags;
	if (parse_mode(mode, &flags) < 0) {
		errno = EINVAL;
		return NULL;
	}
	return make_file(fd, flags);
}

int fclose(FILE *f)
{
	int rc = fflush(f);
	if (close(f->fd) < 0) {
		rc = EOF;
	}
	if (f->flags & F_ALLOC) {
		FILE **link = &opened;
		while (*link != f) {
			link = &(*link)->next;
		}
		*link = f->next;
		free(f);
	}
	return rc;
}

/* Called by exit() (stdlib.c). */
void __stdio_exit(void)
{
	fflush(NULL);
}

int fileno(FILE *f) { return f->fd; }
int feof(FILE *f) { return (f->flags & F_EOF) != 0; }
int ferror(FILE *f) { return (f->flags & F_ERR) != 0; }
void clearerr(FILE *f) { f->flags &= ~(F_EOF | F_ERR); }

/* --- input --- */

static int refill(FILE *f)
{
	if (f->flags & (F_EOF | F_ERR)) {
		return EOF;
	}
	if (f == stdin) {
		fflush(stdout); /* the prompt before the input */
	}
	long n = read(f->fd, f->buf, f->size);
	if (n <= 0) {
		f->flags |= n == 0 ? F_EOF : F_ERR;
		return EOF;
	}
	f->rpos = 0;
	f->rlen = (size_t)n;
	return 0;
}

int fgetc(FILE *f)
{
	if (f->unget != EOF) {
		int c = f->unget;
		f->unget = EOF;
		return c;
	}
	if (f->rpos == f->rlen && refill(f) == EOF) {
		return EOF;
	}
	return f->buf[f->rpos++];
}

int getc(FILE *f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }

int ungetc(int c, FILE *f)
{
	if (c == EOF || f->unget != EOF) {
		return EOF;
	}
	f->unget = (unsigned char)c;
	f->flags &= ~F_EOF;
	return c;
}

char *fgets(char *s, int size, FILE *f)
{
	int n = 0;
	while (n + 1 < size) {
		int c = fgetc(f);
		if (c == EOF) {
			break;
		}
		s[n++] = (char)c;
		if (c == '\n') {
			break;
		}
	}
	if (n == 0 || size <= 0) {
		return NULL;
	}
	s[n] = '\0';
	return s;
}

size_t fread(void *buf, size_t size, size_t count, FILE *f)
{
	unsigned char *p = buf;
	size_t want = size * count, got = 0;
	if (size && want / size != count) {
		errno = EINVAL;
		return 0;
	}
	while (got < want) {
		int c = fgetc(f);
		if (c == EOF) {
			break;
		}
		p[got++] = (unsigned char)c;
		/* Take the rest of the buffer in one go. */
		size_t avail = f->rlen - f->rpos;
		size_t k = want - got < avail ? want - got : avail;
		memcpy(p + got, f->buf + f->rpos, k);
		f->rpos += k;
		got += k;
	}
	return size ? got / size : 0;
}

/* --- output --- */

static int out(FILE *f, const char *s, size_t n)
{
	if (!(f->flags & F_WRITE)) {
		f->flags |= F_ERR;
		errno = EBADF;
		return EOF;
	}
	if (f->flags & F_UNBUF) {
		while (n) {
			long w = write(f->fd, s, n);
			if (w <= 0) {
				f->flags |= F_ERR;
				return EOF;
			}
			s += w;
			n -= (size_t)w;
		}
		return 0;
	}
	bool newline = false;
	while (n) {
		if (f->wlen == f->size && flush_out(f) == EOF) {
			return EOF;
		}
		size_t k = f->size - f->wlen < n ? f->size - f->wlen : n;
		memcpy(f->buf + f->wlen, s, k);
		newline |= (f->flags & F_LINE) && memchr(s, '\n', k);
		f->wlen += k;
		s += k;
		n -= k;
	}
	return newline ? flush_out(f) : 0;
}

int fputc(int c, FILE *f)
{
	char ch = (char)c;
	return out(f, &ch, 1) == EOF ? EOF : (unsigned char)c;
}

int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }

int fputs(const char *s, FILE *f)
{
	return out(f, s, strlen(s)) == EOF ? EOF : 0;
}

int puts(const char *s)
{
	return fputs(s, stdout) == EOF || fputc('\n', stdout) == EOF ? EOF : 0;
}

size_t fwrite(const void *buf, size_t size, size_t count, FILE *f)
{
	size_t n = size * count;
	if (!n || (size && n / size != count)) {
		return 0;
	}
	return out(f, buf, n) == EOF ? 0 : count;
}

/* --- formatted output --- */

static void emit_file(void *ctx, const char *s, size_t n)
{
	out(ctx, s, n);
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
	int n = format(emit_file, f, fmt, ap);
	return ferror(f) ? -1 : n;
}

int vprintf(const char *fmt, va_list ap)
{
	return vfprintf(stdout, fmt, ap);
}

int fprintf(FILE *f, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vfprintf(f, fmt, ap);
	va_end(ap);
	return n;
}

int printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vfprintf(stdout, fmt, ap);
	va_end(ap);
	return n;
}

struct string_sink {
	char *s;
	size_t size, len; /* len counts everything, stored or not */
};

static void emit_string(void *ctx, const char *s, size_t n)
{
	struct string_sink *k = ctx;
	if (k->len + 1 < k->size) {
		size_t room = k->size - 1 - k->len;
		memcpy(k->s + k->len, s, n < room ? n : room);
	}
	k->len += n;
}

int vsnprintf(char *s, size_t size, const char *fmt, va_list ap)
{
	struct string_sink k = { s, size, 0 };
	int n = format(emit_string, &k, fmt, ap);
	if (size) {
		s[k.len < size ? k.len : size - 1] = '\0';
	}
	return n;
}

int vsprintf(char *s, const char *fmt, va_list ap)
{
	return vsnprintf(s, (size_t)-1 / 2, fmt, ap);
}

int snprintf(char *s, size_t size, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(s, size, fmt, ap);
	va_end(ap);
	return n;
}

int sprintf(char *s, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsprintf(s, fmt, ap);
	va_end(ap);
	return n;
}

void perror(const char *prefix)
{
	int err = errno;
	if (prefix && *prefix) {
		fprintf(stderr, "%s: %s\n", prefix, strerror(err));
	} else {
		fprintf(stderr, "%s\n", strerror(err));
	}
}
