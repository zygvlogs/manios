/* view [FILE] -- shows a file's text, with line numbers, in a window.
 *
 * Up to 256 KiB of it is read; what isn't text shows as dots. Keys:
 * Up, Down, PgUp, PgDn, Home, End, Left, Right scroll; Space and b page;
 * g and G go to the top and the end; q or Escape closes. The File box
 * at the top (Tab goes to it) opens another file. It only shows files:
 * it does not change them. */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ui.h>
#include <zkt_abi.h>

#define MAX_BYTES (256 * 1024)
#define BINARY_PROBE 512

struct app {
	struct ui *ui;
	struct ui_widget *path, *text, *status;
	int shown_top, shown_total;
};

static void report(struct app *app, const char *what, const char *why)
{
	char msg[ZKT_PATH_MAX + 64];
	snprintf(msg, sizeof(msg), "%s\n%s", what, why);
	ui_message(app->ui, msg);
}

static void update_status(struct app *app, bool force)
{
	int top = ui_text_top(app->text), total = ui_text_lines(app->text);
	if (force || top != app->shown_top || total != app->shown_total) {
		char line[64];
		snprintf(line, sizeof(line), "line %5d of %-5d", total ? top + 1 : 0, total);
		ui_set_text(app->status, line);
		app->shown_top = top;
		app->shown_total = total;
	}
}

static void load(struct app *app, const char *path)
{
	int fd = open(path, OREAD);
	if (fd < 0) {
		report(app, path, strerror(errno));
		return;
	}
	struct zkt_dirent st;
	if (fstat(fd, &st) == 0 && st.type != ZKT_TYPE_FILE) {
		close(fd);
		report(app, path, st.type == ZKT_TYPE_DIR ? "is a directory" : "is not a regular file");
		return;
	}
	char *data = malloc(MAX_BYTES + 1);
	if (!data) {
		close(fd);
		report(app, path, "out of memory");
		return;
	}
	size_t len = 0;
	long n;
	while (len < MAX_BYTES && (n = read(fd, data + len, MAX_BYTES - len)) > 0) {
		len += (size_t)n;
	}
	bool more = len == MAX_BYTES && read(fd, data + len, 1) > 0;
	close(fd);

	bool binary = memchr(data, 0, len < BINARY_PROBE ? len : BINARY_PROBE) != NULL;
	size_t out = 0;
	for (size_t i = 0; i < len; i++) {
		if (data[i] == '\r' && i + 1 < len && data[i + 1] == '\n') {
			continue; /* DOS line ends */
		}
		data[out++] = data[i] ? data[i] : 1; /* a NUL would end the text: a dot instead */
	}
	data[out] = '\0';
	ui_set_text(app->text, data);
	ui_text_numbers(app->text, true);
	free(data);

	ui_set_text(app->path, path);
	char title[ZKT_PATH_MAX + 16];
	snprintf(title, sizeof(title), "View %s", path);
	ui_set_title(app->ui, title);
	update_status(app, true);
	if (binary || more) {
		report(app, path, binary ? "does not look like text" : "is longer than 256 KiB: the rest is not shown");
	}
	ui_focus(app->text);
}

static void open_typed(struct ui_widget *w, void *arg)
{
	struct app *app = arg;
	load(app, ui_get_text(w));
}

static void synth(struct ui *ui, int key)
{
	struct win_event e = { .type = WIN_KEY, .key = key };
	ui_handle(ui, &e);
}

static bool typed(struct ui *ui, int key, void *arg)
{
	struct app *app = arg;
	if (ui_has_focus(app->path)) {
		return false; /* typing a name */
	}
	switch (key) {
	case 'q':
	case 'Q':
	case 0x1B:
		ui_quit(ui);
		return true;
	case ' ':
		synth(ui, ZKT_KEY_PGDN);
		return true;
	case 'b':
		synth(ui, ZKT_KEY_PGUP);
		return true;
	case 'g':
		synth(ui, ZKT_KEY_HOME);
		return true;
	case 'G':
		synth(ui, ZKT_KEY_END);
		return true;
	}
	return false;
}

static void tick(struct ui *ui, void *arg)
{
	(void)ui;
	update_status(arg, false);
}

int main(int argc, char **argv)
{
	if (argc > 2) {
		fprintf(stderr, "usage: view [FILE]\n");
		return 1;
	}
	static struct app app;
	app.ui = ui_open("View", 520, 380);
	if (!app.ui) {
		fprintf(stderr, "view: no window: %s\n", strerror(errno));
		return 1;
	}
	struct ui_widget *root = ui_root(app.ui);
	struct ui_widget *bar = ui_hbox(root, 6);
	struct ui_widget *label = ui_label(bar, "File");
	ui_set_text_color(label, ui_theme.dim);
	app.path = ui_entry(bar, "", ZKT_PATH_MAX, open_typed, &app);
	ui_set_expand(app.path, 1);
	app.text = ui_text(root, true);
	ui_set_expand(app.text, 1);
	app.status = ui_label(root, "line     0 of 0    ");
	ui_set_text_color(app.status, ui_theme.dim);
	app.shown_top = app.shown_total = -1;
	ui_on_key(app.ui, typed, &app);
	ui_set_timer(app.ui, 200, tick, &app);
	ui_update(app.ui);
	if (argc == 2) {
		load(&app, argv[1]);
	} else {
		ui_focus(app.path);
		ui_set_text(app.text, "Type a file's name above, and press Enter.\n\nFrom the file browser (files), Enter opens the selected file here.");
	}
	ui_run(app.ui);
	ui_free(app.ui);
	return 0;
}
