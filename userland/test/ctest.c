/* libc conformance test, run by the kernel's boot self-test after utest
 * (zkt/kernel/user_selftest.c). Prints only failures and a summary;
 * exits 0 only if every check passed. */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <manios.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
	      && tolower('Q') == 'q' && isxdigit('F') && !isalpha('1') && ispunct('!'), "ctype");
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
	check(malloc(0) == NULL && realloc(NULL, 8) != NULL, "malloc(0), realloc(NULL)");
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

	check(run("/boot/test/fault", "doublefree") == 134, "a double free aborts");
	check(run("/boot/test/fault", "assert") == 134, "a failed assert aborts");
}

int main(void)
{
	heap_start = sbrk(0);
	test_printf();
	test_strtol();
	test_strings();
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
