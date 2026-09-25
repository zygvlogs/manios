/* ls [-l] [PATHS...] -- list directories (sorted), or name files. -l
 * adds the type and size. Directories are shown with a trailing '/'. */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int long_format;

static const char *type_name(uint32_t type)
{
	return type == ZKT_TYPE_DIR ? "dir" : type == ZKT_TYPE_DEVICE ? "dev" : "file";
}

static void show(const struct zkt_dirent *d)
{
	const char *slash = d->type == ZKT_TYPE_DIR ? "/" : "";
	if (long_format) {
		printf("%-4s %8lu  %s%s\n", type_name(d->type), (unsigned long)d->size, d->name, slash);
	} else {
		printf("%s%s\n", d->name, slash);
	}
}

static int by_name(const void *a, const void *b)
{
	return strcmp(((const struct zkt_dirent *)a)->name, ((const struct zkt_dirent *)b)->name);
}

static int list(const char *path)
{
	int fd = open(path, OREAD);
	struct zkt_dirent self;
	if (fd < 0 || fstat(fd, &self) < 0) {
		fprintf(stderr, "ls: %s: %s\n", path, strerror(errno));
		return 1;
	}
	if (self.type != ZKT_TYPE_DIR) {
		strncpy(self.name, path, ZKT_NAME_MAX);
		show(&self);
		close(fd);
		return 0;
	}

	struct zkt_dirent *entries = NULL;
	size_t count = 0, cap = 0;
	long n;
	for (;;) {
		if (count == cap) {
			cap = cap ? cap * 2 : 16;
			struct zkt_dirent *grown = realloc(entries, cap * sizeof(*entries));
			if (!grown) {
				fprintf(stderr, "ls: %s: out of memory\n", path);
				free(entries);
				close(fd);
				return 1;
			}
			entries = grown;
		}
		n = read(fd, entries + count, (cap - count) * sizeof(*entries));
		if (n <= 0) {
			break;
		}
		count += (size_t)n / sizeof(*entries);
	}
	close(fd);
	if (n < 0) {
		fprintf(stderr, "ls: %s: %s\n", path, strerror(errno));
	}
	qsort(entries, count, sizeof(*entries), by_name);
	for (size_t i = 0; i < count; i++) {
		show(&entries[i]);
	}
	free(entries);
	return n < 0;
}

int main(int argc, char **argv)
{
	int i = 1;
	if (argc > 1 && !strcmp(argv[1], "-l")) {
		long_format = 1;
		i++;
	}
	if (i == argc) {
		return list(".");
	}
	int rc = 0;
	for (int first = i; i < argc; i++) {
		if (argc - first > 1) {
			printf("%s%s:\n", i > first ? "\n" : "", argv[i]);
		}
		rc |= list(argv[i]);
	}
	return rc;
}
