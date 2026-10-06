/* files [DIRECTORY] -- a file browser: the directory's entries in a list,
 * folders first, with their sizes. Enter or a double click opens a
 * folder, or a file in `view`; Backspace goes up; type a path in the box
 * at the top (Tab goes to it) and press Enter to go there; F5 lists the
 * directory again. It only looks: it does not change files. */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ui.h>
#include <zkt_abi.h>

#define FILES_KEY_F5 (ZKT_KEY_F1 + 4)

struct app {
	struct ui *ui;
	struct ui_widget *where, *list, *status;
	char path[ZKT_PATH_MAX + 1];
	struct zkt_dirent *entries;
	int count;
	bool has_parent; /* row 0 of the list is ".." */
};

/* Folders first, then by name, ignoring case. */
static int by_name(const void *pa, const void *pb)
{
	const struct zkt_dirent *a = pa, *b = pb;
	int da = a->type == ZKT_TYPE_DIR, db = b->type == ZKT_TYPE_DIR;
	return da != db ? db - da : strcasecmp(a->name, b->name);
}

/* "/a//b/../c/" -> "/a/c": a path from the root, without . or .. in it. */
static void normalize(char *path)
{
	char out[ZKT_PATH_MAX + 1];
	size_t n = 0;
	char *save = NULL;
	out[0] = '\0';
	for (char *part = strtok_r(path, "/", &save); part; part = strtok_r(NULL, "/", &save)) {
		if (!strcmp(part, ".")) {
			continue;
		}
		if (!strcmp(part, "..")) {
			char *slash = strrchr(out, '/');
			n = slash ? (size_t)(slash - out) : 0;
			out[n] = '\0';
			continue;
		}
		if (n + strlen(part) + 1 < sizeof(out)) {
			out[n++] = '/';
			strcpy(out + n, part);
			n += strlen(part);
		}
	}
	strcpy(path, n ? out : "/");
}

static void join(char *out, size_t size, const char *dir, const char *name)
{
	snprintf(out, size, "%s%s%s", dir, strcmp(dir, "/") ? "/" : "", name);
}

static void human(uint32_t n, char *out, size_t size)
{
	if (n < 1024) {
		snprintf(out, size, "%u B", (unsigned)n);
	} else if (n < 1024 * 1024) {
		snprintf(out, size, "%u.%u KB", (unsigned)(n / 1024), (unsigned)(n % 1024 * 10 / 1024));
	} else {
		snprintf(out, size, "%u.%u MB", (unsigned)(n >> 20), (unsigned)((n & 0xFFFFF) * 10 >> 20));
	}
}

static const struct zkt_dirent *entry_at(const struct app *app, int row)
{
	int i = row - app->has_parent;
	return i >= 0 && i < app->count ? &app->entries[i] : NULL;
}

static void describe(struct ui_widget *w, void *arg)
{
	struct app *app = arg;
	(void)w;
	char line[ZKT_PATH_MAX + 64], size[24];
	const struct zkt_dirent *e = entry_at(app, ui_list_selected(app->list));
	if (!e) {
		snprintf(line, sizeof(line), "%d items", app->count);
	} else if (e->type == ZKT_TYPE_FILE) {
		human(e->size, size, sizeof(size));
		snprintf(line, sizeof(line), "%s: %s", e->name, size);
	} else {
		snprintf(line, sizeof(line), "%s: %s", e->name,
		         e->type == ZKT_TYPE_DIR ? "folder" : e->type == ZKT_TYPE_DEVICE ? "device" : "pipe");
	}
	ui_set_text(app->status, line);
}

/* Lists `path` (already normalized). On failure, says why and stays. */
static bool go(struct app *app, const char *path)
{
	int fd = open(path, OREAD);
	if (fd < 0) {
		ui_message(app->ui, strerror(errno));
		return false;
	}
	struct zkt_dirent probe;
	if (fstat(fd, &probe) == 0 && probe.type != ZKT_TYPE_DIR) {
		close(fd);
		ui_message(app->ui, "Not a folder");
		return false;
	}
	int cap = 64, n = 0;
	struct zkt_dirent *entries = malloc((size_t)cap * sizeof(*entries));
	while (entries) {
		if (n == cap) {
			struct zkt_dirent *bigger = realloc(entries, (size_t)cap * 2 * sizeof(*entries));
			if (!bigger) {
				break;
			}
			entries = bigger;
			cap *= 2;
		}
		if (read(fd, &entries[n], sizeof(entries[n])) != sizeof(entries[n])) {
			break;
		}
		if (strcmp(entries[n].name, ".") && strcmp(entries[n].name, "..")) {
			n++;
		}
	}
	close(fd);
	if (!entries) {
		ui_message(app->ui, "Out of memory");
		return false;
	}
	qsort(entries, (size_t)n, sizeof(*entries), by_name);
	free(app->entries);
	app->entries = entries;
	app->count = n;
	strlcpy(app->path, path, sizeof(app->path));
	app->has_parent = strcmp(path, "/") != 0;

	ui_list_clear(app->list);
	if (app->has_parent) {
		ui_list_add(app->list, "..\tup", ui_theme.focus, NULL);
	}
	for (int i = 0; i < n; i++) {
		const struct zkt_dirent *e = &entries[i];
		char text[ZKT_NAME_MAX + 32], size[24] = "";
		gfx_color color = e->type == ZKT_TYPE_DIR ? ui_theme.focus : e->type == ZKT_TYPE_FILE ? 0 : ui_theme.dim;
		if (e->type == ZKT_TYPE_FILE) {
			human(e->size, size, sizeof(size));
		}
		snprintf(text, sizeof(text), "%s%s\t%s", e->name, e->type == ZKT_TYPE_DIR ? "/" : "", size);
		ui_list_add(app->list, text, color, (void *)e);
	}
	ui_list_select(app->list, 0);
	ui_set_text(app->where, path);
	describe(NULL, app);
	return true;
}

static void up(struct app *app)
{
	char parent[ZKT_PATH_MAX + 1];
	join(parent, sizeof(parent), app->path, "..");
	normalize(parent);
	char keep[ZKT_NAME_MAX + 1];
	const char *slash = strrchr(app->path, '/');
	strlcpy(keep, slash ? slash + 1 : "", sizeof(keep));
	if (go(app, parent)) {
		for (int i = 0; i < app->count; i++) { /* the folder we left stays selected */
			if (!strcmp(app->entries[i].name, keep)) {
				ui_list_select(app->list, i + app->has_parent);
				describe(NULL, app);
				break;
			}
		}
	}
}

static void open_selected(struct ui_widget *w, void *arg)
{
	struct app *app = arg;
	(void)w;
	int row = ui_list_selected(app->list);
	if (app->has_parent && row == 0) {
		up(app);
		return;
	}
	const struct zkt_dirent *e = entry_at(app, row);
	if (!e) {
		return;
	}
	char path[ZKT_PATH_MAX + 1];
	join(path, sizeof(path), app->path, e->name);
	if (e->type == ZKT_TYPE_DIR) {
		go(app, path);
	} else if (e->type == ZKT_TYPE_FILE) {
		char *argv[] = { "view", path, NULL };
		if (spawn("/bin/view", argv) < 0) {
			ui_message(app->ui, "Can't start view");
		}
	} else {
		ui_message(app->ui, "Only files and folders\ncan be opened");
	}
}

static void typed_path(struct ui_widget *w, void *arg)
{
	struct app *app = arg;
	char path[ZKT_PATH_MAX + 1];
	const char *text = ui_get_text(w);
	if (text[0] == '/') {
		strlcpy(path, text, sizeof(path));
	} else {
		join(path, sizeof(path), app->path, text);
	}
	normalize(path);
	if (go(app, path)) {
		ui_focus(app->list);
	}
}

static bool typed(struct ui *ui, int key, void *arg)
{
	struct app *app = arg;
	(void)ui;
	if (ui_has_focus(app->where)) {
		return false;
	}
	if (key == '\b' || key == 0x7F) {
		if (app->has_parent) {
			up(app);
		}
		return true;
	}
	if (key == FILES_KEY_F5) {
		char keep[ZKT_PATH_MAX + 1];
		strlcpy(keep, app->path, sizeof(keep));
		go(app, keep);
		return true;
	}
	return false;
}

/* Programs it started that have finished are collected. */
static void tick(struct ui *ui, void *arg)
{
	int status;
	(void)ui;
	(void)arg;
	while (reap(&status) > 0) {
	}
}

int main(int argc, char **argv)
{
	if (argc > 2) {
		fprintf(stderr, "usage: files [DIRECTORY]\n");
		return 1;
	}
	static struct app app;
	app.ui = ui_open("Files", 420, 300);
	if (!app.ui) {
		fprintf(stderr, "files: no window: %s\n", strerror(errno));
		return 1;
	}
	struct ui_widget *root = ui_root(app.ui);
	app.where = ui_entry(root, "", ZKT_PATH_MAX, typed_path, &app);
	app.list = ui_list(root, open_selected, &app);
	ui_set_expand(app.list, 1);
	ui_on_change(app.list, describe, &app);
	app.status = ui_label(root, "");
	ui_set_text_color(app.status, ui_theme.dim);
	ui_on_key(app.ui, typed, &app);
	ui_set_timer(app.ui, 1000, tick, &app);
	ui_update(app.ui);
	ui_focus(app.list);

	char start[ZKT_PATH_MAX + 1] = "/";
	if (argc == 2) {
		if (argv[1][0] == '/') {
			strlcpy(start, argv[1], sizeof(start));
		} else {
			char cwd[ZKT_PATH_MAX + 1];
			join(start, sizeof(start), getcwd(cwd, sizeof(cwd)) ? cwd : "/", argv[1]);
		}
		normalize(start);
	}
	if (!go(&app, start)) {
		go(&app, "/");
	}
	ui_run(app.ui);
	ui_free(app.ui);
	free(app.entries);
	return 0;
}
