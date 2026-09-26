/* libc conformance test, run by the kernel's boot self-test after utest
 * (zkt/kernel/user_selftest.c). Prints only failures and a summary;
 * exits 0 only if every check passed. */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <regex.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* From <manios.h>, whose open() and fstat() are ManiOS's own: this test
 * uses the POSIX ones (<fcntl.h>, <sys/stat.h>). */
int spawn(const char *path, char *const argv[]);
int wait(int pid, int *status);
void *sbrk(intptr_t increment);

static int checks, failures;
static char *heap_start; /* sbrk(0) before the first malloc */

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failures++;
		printf("ctest: FAIL: %s\n", what);
	}
}

/* snprintf must produce `want`, and return its length. */
#define CHECKF(want, ...)                                                             \
	do {                                                                              \
		char buf_[96];                                                                \
		int n_ = snprintf(buf_, sizeof(buf_), __VA_ARGS__);                           \
		checks++;                                                                     \
		if (strcmp(buf_, want) || n_ != (int)strlen(want)) {                          \
			failures++;                                                               \
			printf("ctest: FAIL: printf(%s) gave \"%s\" (%d), not \"%s\"\n", #__VA_ARGS__, \
			       buf_, n_, want);                                                   \
		}                                                                             \
	} while (0)

static void test_printf(void)
{
	CHECKF("42 -42 0", "%d %i %d", 42, -42, 0);
	CHECKF("[  42] [42  ] [0042] [+42] [ 42]", "[%4d] [%-4d] [%04d] [%+d] [% d]", 42, 42, 42, 42, 42);
	CHECKF("-2147483648 2147483647", "%d %d", INT_MIN, INT_MAX);
	CHECKF("4294967295 ff FF 0xff 777 0777", "%u %x %X %#x %o %#o", UINT_MAX, 255, 255, 255, 511, 511);
	CHECKF("00042 [   00042] [] [0]", "%.5d [%8.5d] [%.0d] [%d]", 42, 42, 0, 0);
	CHECKF("[-0042] [ -42]", "[%05d] [%4d]", -42, -42);
	CHECKF("abc [ab] [  abc] [abc  ]", "%s [%.2s] [%5s] [%-5s]", "abc", "abc", "abc", "abc");
	CHECKF("x% [   7] [7   ] [7   ]", "%c%% [%*d] [%-*d] [%*d]", 'x', 4, 7, 4, 7, -4, 7);
	CHECKF("0x1000", "%p", (void *)0x1000);
	CHECKF("18446744073709551615 -9223372036854775808", "%llu %lld", ULLONG_MAX, LLONG_MIN);
	CHECKF("123456789012 1234567890abcdef", "%lld %llx", 123456789012LL, 0x1234567890abcdefULL);
	CHECKF("4000000000 7", "%lu %zu", 4000000000UL, sizeof(char[7]));

	char small[5];
	check(snprintf(small, sizeof(small), "%s", "abcdefgh") == 8 && !strcmp(small, "abcd"),
	      "snprintf truncates and returns the full length");
	check(snprintf(NULL, 0, "%d", 12345) == 5, "snprintf(NULL, 0) measures");
}

static void test_strtol(void)
{
	char *end;
	check(strtol("123", &end, 10) == 123 && !*end, "strtol decimal");
	check(strtol("-0x1f", NULL, 0) == -31 && strtol("0755", NULL, 0) == 493
	      && strtol("zz", NULL, 36) == 1295, "strtol bases");
	const char *s = " +42abc";
	check(strtol(s, &end, 10) == 42 && end == s + 4, "strtol stops at the first non-digit");
	check(strtol("xyz", &end, 10) == 0 && !strcmp(end, "xyz"), "strtol without digits");
	errno = 0;
	check(strtol("-2147483648", NULL, 10) == LONG_MIN && errno == 0, "strtol LONG_MIN");
	check(strtol("2147483648", NULL, 10) == LONG_MAX && errno == ERANGE, "strtol overflow");
	errno = 0;
	check(strtol("-2147483649", NULL, 10) == LONG_MIN && errno == ERANGE, "strtol underflow");
	errno = 0;
	check(strtoul("4294967295", NULL, 10) == ULONG_MAX && errno == 0, "strtoul ULONG_MAX");
	check(strtoul("4294967296", NULL, 10) == ULONG_MAX && errno == ERANGE, "strtoul overflow");
	check(atoi("  -17") == -17 && abs(-5) == 5, "atoi, abs");

	errno = 0;
	check(strtoll("9223372036854775807", NULL, 10) == LLONG_MAX && errno == 0
	      && strtoll("-9223372036854775808", NULL, 10) == LLONG_MIN && errno == 0,
	      "strtoll limits");
	check(strtoll("9223372036854775808", NULL, 10) == LLONG_MAX && errno == ERANGE,
	      "strtoll overflow");
	errno = 0;
	check(strtoull("18446744073709551615", NULL, 10) == ULLONG_MAX && errno == 0
	      && strtoull("0x123456789abcdef0", NULL, 0) == 0x123456789abcdef0ULL,
	      "strtoull");
	check(strtoull("18446744073709551616", NULL, 10) == ULLONG_MAX && errno == ERANGE,
	      "strtoull overflow");

	const char *why;
	check(strtonum("42", 1, 100, &why) == 42 && why == NULL, "strtonum");
	check(strtonum("0", 1, 100, &why) == 0 && !strcmp(why, "too small")
	      && strtonum("101", 1, 100, &why) == 0 && !strcmp(why, "too large")
	      && strtonum("4x", 1, 100, &why) == 0 && !strcmp(why, "invalid"),
	      "strtonum rejects what is out of range or not a number");
}

static void test_strings(void)
{
	char buf[32];
	check(!strcmp(strstr("hello world", "o w"), "o world") && !strstr("abc", "abd")
	      && strstr("abc", "") != NULL, "strstr");
	check(strspn("aabbc", "ab") == 4 && strcspn("hello, world", ",") == 5
	      && !strcmp(strpbrk("a=b", "=:"), "=b"), "strspn, strcspn, strpbrk");
	check(strrchr("a/b/c", '/')[1] == 'c' && strchr("abc", '\0') && !strchr("abc", 'z'),
	      "strchr, strrchr");
	strcpy(buf, "one two  three");
	char *save, *t1 = strtok_r(buf, " ", &save), *t2 = strtok_r(NULL, " ", &save);
	char *t3 = strtok_r(NULL, " ", &save), *t4 = strtok_r(NULL, " ", &save);
	check(t1 && t2 && t3 && !t4 && !strcmp(t1, "one") && !strcmp(t2, "two") && !strcmp(t3, "three"),
	      "strtok_r");
	memset(buf, 'x', sizeof(buf));
	strncpy(buf, "ab", 5);
	check(!memcmp(buf, "ab\0\0\0x", 6), "strncpy pads with NULs");
	strcpy(buf, "ab");
	strncat(buf, "cdef", 2);
	check(!strcmp(buf, "abcd"), "strncat");
	strcpy(buf, "0123456789");
	memmove(buf + 2, buf, 5);
	check(!memcmp(buf, "0101234789", 10), "memmove forward overlap");
	memmove(buf, buf + 3, 5);
	check(!memcmp(buf, "1234734789", 10), "memmove backward overlap");
	check(memchr("abc", 'c', 3) && !memchr("abc", 'c', 2), "memchr");
	char *d = strdup("copy");
	check(d && !strcmp(d, "copy"), "strdup");
	free(d);
	check(!strcmp(strerror(ENOENT), "no such file or directory"), "strerror");
	check(isdigit('7') && !isdigit('a') && isspace('\t') && toupper('q') == 'Q'
	      && tolower('Q') == 'q' && isxdigit('F') && !isalpha('1') && ispunct('!')
	      && isblank('\t') && !isblank('\n'), "ctype");

	strcpy(buf, "a,,b");
	char *rest = buf, *f1 = strsep(&rest, ","), *f2 = strsep(&rest, ",");
	char *f3 = strsep(&rest, ",");
	check(!strcmp(f1, "a") && !strcmp(f2, "") && !strcmp(f3, "b") && rest == NULL
	      && strsep(&rest, ",") == NULL, "strsep keeps empty fields");
	check(!strcasecmp("ManiOS", "MANIOS") && strcasecmp("a", "B") < 0
	      && !strncasecmp("abcX", "ABCy", 3) && strncasecmp("abcX", "ABCy", 4) < 0,
	      "strcasecmp, strncasecmp");
	strcpy(buf, "/a/b/c.txt");
	check(!strcmp(basename(buf), "c.txt") && !strcmp(dirname(buf), "/a/b")
	      && !strcmp(basename("/"), "/") && !strcmp(dirname("file"), "."),
	      "basename, dirname (OpenBSD's)");
	check(reallocarray(NULL, 0x10000, 0x10000) == NULL && errno == ENOMEM,
	      "reallocarray catches overflow");
	strcpy(buf, "ab");
	check(strlcat(buf, "cdef", 5) == 6 && !strcmp(buf, "abcd") && strnlen("abc", 2) == 2,
	      "strlcat truncates and returns the full length; strnlen");
	char *s = NULL;
	check(asprintf(&s, "%d-%s", 42, "x") == 4 && s && !strcmp(s, "42-x"), "asprintf");
	free(s);
	check(isgraph('a') && !isgraph(' ') && isascii(0x7f) && !isascii(0x80), "isgraph, isascii");
}

/* 64-bit division (divdi3.c): operands the compiler can't fold. */
static void test_divide(void)
{
	volatile uint64_t max = UINT64_MAX, three = 3, seven = 7, big = 0x100000001ULL;
	volatile uint64_t ten_g = 10000000000ULL, two32 = 0x100000000ULL;
	volatile int64_t m9 = -9, two = 2, mtwo = -2, min = INT64_MIN, one = 1;
	/* The high word's remainder carries into the low word's division. */
	check(max / three == 6148914691236517205ULL && max % three == 0 && (max - 1) % three == 2
	      && two32 / three == 0x55555555ULL && two32 % three == 1
	      && ten_g / seven == 1428571428ULL && ten_g % seven == 4,
	      "64-bit unsigned division by a 32-bit number");
	/* 2^64 - 1 = (2^32 - 1)(2^32 + 1) */
	check(max / big == 0xFFFFFFFFULL && max % big == 0 && (max - 5) / big == 0xFFFFFFFEULL
	      && (max - 5) % big == 0x100000001ULL - 5, "64-bit unsigned division by a 64-bit number");
	check(m9 / two == -4 && m9 % two == -1 && -m9 / mtwo == -4 && -m9 % mtwo == 1
	      && min / one == INT64_MIN, "64-bit signed division truncates toward zero");
}

static void test_regex(void)
{
	regex_t re;
	regmatch_t m[2];
	check(regcomp(&re, "^b(an)+a$", REG_EXTENDED) == 0
	      && regexec(&re, "banana", 2, m, 0) == 0 && m[1].rm_so == 3 && m[1].rm_eo == 5
	      && regexec(&re, "bnana", 0, NULL, 0) == REG_NOMATCH, "regcomp, regexec, subexpressions");
	regfree(&re);
	char msg[64];
	int rc = regcomp(&re, "[", 0);
	check(rc == REG_EBRACK && regerror(rc, &re, msg, sizeof(msg)) > 0
	      && !strcmp(msg, "brackets ([ ]) not balanced"), "regerror");
}

/* The POSIX layer over ManiOS's calls (stat.c, dirent.c, posix.c). */
static void test_posix(void)
{
	struct stat a, b, c;
	check(stat("/boot/etc/motd", &a) == 0 && S_ISREG(a.st_mode) && a.st_size == 19
	      && stat("/boot/etc/../etc//motd", &b) == 0 && a.st_ino == b.st_ino
	      && stat("/boot/etc/rc.cpu", &c) == 0 && c.st_ino != a.st_ino,
	      "stat: type, size, and st_ino by path");
	check(stat("/boot", &a) == 0 && S_ISDIR(a.st_mode) && a.st_nlink == 1
	      && stat("/dev/cons", &b) == 0 && S_ISCHR(b.st_mode), "stat of a directory and a device");
	errno = 0;
	check(stat("/no/such", &a) == -1 && errno == ENOENT, "stat of a missing file");
	int fds[2];
	check(pipe(fds) == 0 && fstat(fds[0], &a) == 0 && S_ISFIFO(a.st_mode) && !isatty(fds[0])
	      && errno == ENOTTY, "fstat of a pipe, isatty");
	close(fds[0]);
	close(fds[1]);

	DIR *d = opendir("/boot/etc");
	struct dirent *e;
	int seen = 0;
	while (d && (e = readdir(d))) {
		seen |= !strcmp(e->d_name, "motd") && e->d_type == DT_REG && e->d_namlen == 4;
	}
	check(d && seen, "opendir, readdir");
	if (d) {
		closedir(d);
	}
	d = opendir("/boot");
	check(d && fstatat(dirfd(d), "etc", &a, 0) == 0 && S_ISDIR(a.st_mode)
	      && fstatat(AT_FDCWD, "/boot/etc/motd", &b, 0) == 0 && S_ISREG(b.st_mode),
	      "fstatat, relative to an open directory");
	if (d) {
		closedir(d);
	}
	errno = 0;
	check(!opendir("/boot/etc/motd") && errno == ENOTDIR, "opendir of a file");

	int fd = open("/boot/etc/motd", O_RDONLY);
	check(fd >= 0 && close(fd) == 0, "open, O_RDONLY");
	errno = 0;
	check(open("/boot/etc/new", O_WRONLY | O_CREAT, 0644) == -1 && errno == EROFS,
	      "open can't create files: EROFS");
	errno = 0;
	check(open("/boot/etc/motd", O_WRONLY | O_TRUNC) == -1, "open can't truncate files");
	errno = 0;
	check(open("/boot/etc/motd", O_RDONLY | O_DIRECTORY) == -1 && errno == ENOTDIR,
	      "O_DIRECTORY of a file: ENOTDIR");
	check(access("/boot/etc/motd", R_OK) == 0 && access("/boot/etc/motd", X_OK) == -1
	      && access("/boot", X_OK) == 0 && access("/no/such", F_OK) == -1, "access");
	errno = 0;
	check(mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, 0, 0) == MAP_FAILED && errno == ENODEV,
	      "mmap fails: ENODEV");
	struct winsize ws;
	errno = 0;
	check(ioctl(1, TIOCGWINSZ, &ws) == -1 && errno == ENOTTY, "ioctl fails: ENOTTY");
	check(signal(SIGINT, SIG_IGN) == SIG_DFL && raise(SIGINT) == 0 && signal(SIGINT, SIG_DFL) == SIG_IGN,
	      "signal records a handler; raise of an ignored signal");
	errno = 0;
	check(rename("/boot/etc/motd", "/boot/etc/x") == -1 && errno == EROFS
	      && unlink("/boot/etc/motd") == -1 && errno == EROFS, "rename, unlink: EROFS");
	errno = 0;
	check(unlink("/boot/etc/none") == -1 && errno == ENOENT, "unlink of a missing file: ENOENT");
	check(getenv("PATH") == NULL && geteuid() == 0, "getenv, geteuid");
}

static void test_getopt(void)
{
	char *args[] = { "prog", "-ab", "-c", "one", "-dtwo", "-x", "--", "-file", 0 };
	int argc = 8, got[8], n = 0, c;
	char *c_arg = 0, *d_arg = 0;
	optind = 0;
	opterr = 0;
	while ((c = getopt(argc, args, "abc:d:")) != -1 && n < 8) {
		got[n++] = c;
		if (c == 'c') {
			c_arg = optarg;
		} else if (c == 'd') {
			d_arg = optarg;
		}
	}
	check(n == 5 && got[0] == 'a' && got[1] == 'b' && got[2] == 'c' && got[3] == 'd'
	      && got[4] == '?' && optopt == 'x' && c_arg && !strcmp(c_arg, "one")
	      && d_arg && !strcmp(d_arg, "two") && optind == 7,
	      "getopt: grouped flags, joined and separate arguments, --");

	char *missing[] = { "prog", "-v", "-n", 0 };
	optind = 0;
	int first = getopt(3, missing, ":vn:"), second = getopt(3, missing, ":vn:");
	check(first == 'v' && second == ':' && optopt == 'n' && getopt(3, missing, ":vn:") == -1,
	      "getopt: a missing argument, with a leading ':'");
	char *operand[] = { "prog", "file", "-v", 0 };
	optind = 0;
	check(getopt(3, operand, "v") == -1 && optind == 1, "getopt stops at the first operand");
	static const struct option longs[] = {
		{ "verbose", no_argument, NULL, 'v' },
		{ "name", required_argument, NULL, 'n' },
		{ NULL, 0, NULL, 0 },
	};
	char *largs[] = { "prog", "--verbose", "--name=x", "--name", "y", "-v", "file", 0 };
	char *names[2] = { 0, 0 };
	int k = 0, lv = 0;
	optind = 0;
	while ((c = getopt_long(7, largs, "vn:", longs, NULL)) != -1) {
		if (c == 'v') {
			lv++;
		} else if (c == 'n' && k < 2) {
			names[k++] = optarg;
		}
	}
	check(lv == 2 && k == 2 && !strcmp(names[0], "x") && !strcmp(names[1], "y") && optind == 6,
	      "getopt_long: --flag, --opt=value, --opt value");
	opterr = 1;
	optind = 1;
	check(!strcmp(getprogname(), "ctest"), "getprogname");
}

static int cmp_int(const void *a, const void *b)
{
	int x = *(const int *)a, y = *(const int *)b;
	return x < y ? -1 : x > y;
}

static void test_sort(void)
{
	enum { N = 300 };
	static int v[N];
	uint32_t seed = 12345, sum = 0, sorted_sum = 0;
	for (int i = 0; i < N; i++) {
		seed = seed * 1103515245u + 12345u;
		v[i] = (int)(seed >> 8) % 1000 - 500;
		sum += (uint32_t)v[i];
	}
	qsort(v, N, sizeof(int), cmp_int);
	int ordered = 1;
	for (int i = 0; i < N; i++) {
		sorted_sum += (uint32_t)v[i];
		ordered &= i == 0 || v[i - 1] <= v[i];
	}
	check(ordered && sum == sorted_sum, "qsort sorts, keeping every element");
	int key = v[N / 3];
	int *found = bsearch(&key, v, N, sizeof(int), cmp_int);
	int missing = 1000;
	check(found && *found == key && !bsearch(&missing, v, N, sizeof(int), cmp_int), "bsearch");
}

static void test_malloc(void)
{
	enum { N = 64 };
	static char *p[N];
	static size_t size[N];
	int ok = 1;
	for (int i = 0; i < N; i++) {
		size[i] = (size_t)(i * 37 % 700) + 1;
		p[i] = malloc(size[i]);
		ok &= p[i] && ((uintptr_t)p[i] & 15) == 0;
		if (p[i]) {
			memset(p[i], i, size[i]);
		}
	}
	check(ok, "malloc returns 16-byte-aligned blocks");
	for (int i = 0; i < N; i += 2) {
		free(p[i]);
		p[i] = NULL;
	}
	for (int i = 1; i < N; i += 2) {
		p[i] = realloc(p[i], size[i] * 3);
		for (size_t k = 0; ok && p[i] && k < size[i]; k++) {
			ok &= p[i][k] == (char)i;
		}
		ok &= p[i] != NULL;
	}
	check(ok, "realloc keeps the contents; blocks don't overlap");
	char *z = calloc(1000, 4);
	int zero = z != NULL;
	for (int k = 0; zero && k < 4000; k++) {
		zero &= z[k] == 0;
	}
	check(zero, "calloc zeroes");
	check(calloc(0x10000, 0x10000) == NULL && errno == ENOMEM, "calloc overflow is ENOMEM");
	free(z);
	char *big = malloc(1024 * 1024);
	check(big != NULL, "a 1 MiB allocation grows the heap");
	if (big) {
		big[1024 * 1024 - 1] = 1;
	}
	free(big);
	for (int i = 0; i < N; i++) {
		free(p[i]);
	}

	/* Everything is free, so it must have coalesced into one block
	 * spanning the whole heap (less its header). */
	char *end = sbrk(0);
	big = malloc((size_t)(end - heap_start) - 16);
	check(big != NULL && sbrk(0) == end, "freed blocks coalesce and are reused");
	free(big);
	char *none = malloc(0), *other = malloc(0);
	check(none && other && none != other && realloc(none, 0) == none && realloc(NULL, 8) != NULL,
	      "malloc(0) and realloc(p, 0) give pointers of their own, realloc(NULL)");
	free(none);
	free(other);
}

static int run(const char *path, const char *arg)
{
	char *argv[] = { (char *)path, (char *)arg, 0 };
	int status, pid = spawn(path, argv);
	return pid < 0 || wait(pid, &status) < 0 ? -1 : status;
}

static void test_stdio(void)
{
	char line[64];
	FILE *f = fopen("/boot/etc/motd", "r");
	check(f && fgets(line, sizeof(line), f) && !strcmp(line, "Welcome to ManiOS.\n"),
	      "fopen, fgets");
	check(f && fgetc(f) == EOF && feof(f) && !ferror(f), "end of file");
	if (f) {
		fclose(f);
	}
	f = fopen("/boot/etc/motd", "r");
	char buf[40];
	check(f && fread(buf, 1, 7, f) == 7 && !memcmp(buf, "Welcome", 7), "fread");
	check(f && ungetc('X', f) == 'X' && fgetc(f) == 'X' && fgetc(f) == ' ', "ungetc");
	check(f && fread(buf, 4, 8, f) == 2, "fread counts whole items");
	if (f) {
		fclose(f);
	}
	errno = 0;
	check(!fopen("/no/such", "r") && errno == ENOENT, "fopen of a missing file");
	check(!fopen("/boot/etc/motd", "q") && errno == EINVAL, "fopen with a bad mode");
	check(!fopen("/boot/etc/motd", "w") && errno == EROFS, "fopen for writing on a read-only file");

	f = fopen("/dev/cons", "w");
	check(f && fprintf(f, "%s", "") == 0 && fclose(f) == 0, "a device opened for writing");

	f = fopen("/boot/etc/motd", "r");
	char *text = NULL;
	size_t size = 0;
	check(f && getline(&text, &size, f) == 19 && !strcmp(text, "Welcome to ManiOS.\n")
	      && size >= 20, "getline");
	if (f) {
		while (getline(&text, &size, f) > 0) {
		}
		check(getline(&text, &size, f) == -1 && feof(f), "getline at end of file");
		fclose(f);
	}
	free(text);

	check(freopen("/boot/etc/motd", "r", stdin) == stdin && fgets(line, sizeof(line), stdin)
	      && !strcmp(line, "Welcome to ManiOS.\n"), "freopen");

	f = fopen("/boot/etc/motd", "r");
	size_t len = 0;
	char *ln = f ? fgetln(f, &len) : NULL;
	check(ln && len == 19 && !memcmp(ln, "Welcome to ManiOS.\n", 19) && !fgetln(f, &len),
	      "fgetln");
	check(f && setvbuf(f, NULL, 7, 0) == -1 && errno == EINVAL && setvbuf(f, NULL, _IONBF, 0) == 0,
	      "setvbuf");
	if (f) {
		fclose(f);
	}
	int ends2[2];
	f = pipe(ends2) == 0 && write(ends2[1], "xyz", 3) == 3 && close(ends2[1]) == 0
	    ? fdopen(ends2[0], "r") : NULL;
	errno = 0;
	check(f && fgetc(f) == 'x' && fseek(f, 0, SEEK_SET) == -1 && errno == ESPIPE && fgetc(f) == 'y',
	      "a pipe can't be seeked, and keeps its buffered input");
	if (f) {
		fclose(f);
	}

	/* No SIGPIPE: stdio ends a program whose reader is gone, with the
	 * status a shell shows for SIGPIPE. OpenBSD's yes(1) relies on it;
	 * basename(1) prints one line, so a regression fails, not hangs. */
	int ends[2], saved = dup(1);
	fflush(stdout);
	if (saved >= 0 && pipe(ends) == 0) {
		dup2(ends[0], 1);
		close(ends[0]);
		close(ends[1]);
		int st = run("/bin/basename", "/a/b");
		dup2(saved, 1);
		check(st == 141, "writing to a pipe with no reader ends the program");
	} else {
		check(0, "pipe for the broken pipe check");
	}
	if (saved >= 0) {
		close(saved);
	}

	check(run("/boot/test/fault", "doublefree") == 134, "a double free aborts");
	check(run("/boot/test/fault", "assert") == 134, "a failed assert aborts");
}

int main(void)
{
	heap_start = sbrk(0);
	test_printf();
	test_strtol();
	test_strings();
	test_divide();
	test_regex();
	test_posix();
	test_getopt();
	test_sort();
	test_malloc();
	test_stdio();
	if (failures) {
		printf("ctest: %d of %d checks failed\n", failures, checks);
		return 1;
	}
	printf("ctest: all %d checks passed\n", checks);
	return 0;
}
