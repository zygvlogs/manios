/* widgets -- every libui widget in one window: what the toolkit offers
 * and how it looks, and a program to copy from (docs/gui.md). Each
 * control changes something you can see: the button counts, the check
 * box runs the progress bar, the entry echoes what you type (and Enter
 * shows it in a message), the list says what is selected, the text
 * area counts its lines. Tab goes round the controls. */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <string.h>
#include <ui.h>

struct app {
	struct ui *ui;
	struct ui_widget *clicked, *run, *entry, *echo, *list, *picked, *text, *lines, *bar;
	int clicks, progress;
};

static const char *const FRUIT[] = {
	"Apple", "Banana", "Cherry", "Damson", "Elderberry", "Fig", "Grape", "Honeydew",
	"Kiwi", "Lemon", "Mango", "Nectarine", "Orange", "Papaya", "Quince", "Raspberry",
};

static void on_click(struct ui_widget *w, void *arg)
{
	struct app *app = arg;
	char text[48];
	(void)w;
	app->clicks++;
	snprintf(text, sizeof(text), "clicked %d time%s", app->clicks, app->clicks == 1 ? "" : "s");
	ui_set_text(app->clicked, text);
}

static void on_entry(struct ui_widget *w, void *arg)
{
	struct app *app = arg;
	char text[160];
	snprintf(text, sizeof(text), "echo: %s", ui_get_text(w));
	ui_set_text(app->echo, text);
}

static void on_entered(struct ui_widget *w, void *arg)
{
	struct app *app = arg;
	char text[160];
	snprintf(text, sizeof(text), "You typed:\n%s", ui_get_text(w));
	ui_message(app->ui, text);
}

/* The selected row's name: its text up to the Tab that starts the dim column. */
static void selected_name(const struct app *app, char *out, size_t size)
{
	const char *row = ui_list_text(app->list, ui_list_selected(app->list));
	snprintf(out, size, "%.*s", (int)strcspn(row ? row : "", "\t"), row ? row : "");
}

static void on_pick(struct ui_widget *w, void *arg)
{
	struct app *app = arg;
	char name[32], text[64];
	(void)w;
	selected_name(app, name, sizeof(name));
	snprintf(text, sizeof(text), "selected: %s", name);
	ui_set_text(app->picked, text);
}

static void on_activate(struct ui_widget *w, void *arg)
{
	struct app *app = arg;
	char name[32], text[64];
	(void)w;
	selected_name(app, name, sizeof(name));
	snprintf(text, sizeof(text), "%s!", name);
	ui_message(app->ui, text);
}

static void on_text(struct ui_widget *w, void *arg)
{
	struct app *app = arg;
	char text[64];
	(void)w;
	snprintf(text, sizeof(text), "line %d, column %d of %d", ui_text_line(app->text) + 1,
	         ui_text_column(app->text) + 1, ui_text_lines(app->text));
	ui_set_text(app->lines, text);
}

static void tick(struct ui *ui, void *arg)
{
	struct app *app = arg;
	(void)ui;
	if (ui_checked(app->run)) {
		app->progress = (app->progress + 5) % 105;
		ui_set_progress(app->bar, app->progress, 100);
	}
}

int main(void)
{
	static struct app app;
	app.ui = ui_open("Widgets", 560, 360);
	if (!app.ui) {
		fprintf(stderr, "widgets: no window: %s\n", strerror(errno));
		return 1;
	}
	struct ui_widget *root = ui_root(app.ui);
	struct ui_widget *title = ui_label(root, "libui widgets");
	ui_set_scale(title, 2);
	ui_set_text_color(title, ui_theme.accent);

	struct ui_widget *columns = ui_hbox(root, 10);
	ui_set_expand(columns, 1);

	/* The left column: things you press and type into. */
	struct ui_widget *left = ui_vbox(columns, 6);
	ui_set_expand(left, 1);
	ui_button(left, "Click me", on_click, &app);
	app.clicked = ui_label(left, "clicked 0 times");
	ui_set_text_color(app.clicked, ui_theme.dim);
	ui_separator(left);
	app.run = ui_checkbox(left, "Run the progress bar", false, NULL, NULL);
	app.bar = ui_progress(left);
	ui_separator(left);
	app.entry = ui_entry(left, "", 40, on_entered, &app);
	ui_on_change(app.entry, on_entry, &app);
	app.echo = ui_label(left, "echo: ");
	ui_set_text_color(app.echo, ui_theme.dim);
	ui_spacer(left);

	/* The right: a list, and text. */
	struct ui_widget *right = ui_vbox(columns, 6);
	ui_set_expand(right, 1);
	app.list = ui_list(right, on_activate, &app);
	ui_set_expand(app.list, 2);
	for (size_t i = 0; i < sizeof(FRUIT) / sizeof(FRUIT[0]); i++) {
		char text[32];
		snprintf(text, sizeof(text), "%s\t%zu", FRUIT[i], strlen(FRUIT[i]));
		ui_list_add(app.list, text, 0, NULL);
	}
	ui_on_change(app.list, on_pick, &app);
	app.picked = ui_label(right, "selected: -");
	ui_set_text_color(app.picked, ui_theme.dim);
	app.text = ui_text(right, false);
	ui_set_expand(app.text, 2);
	ui_set_text(app.text, "A text area:\nedit me.");
	ui_on_change(app.text, on_text, &app);
	app.lines = ui_label(right, "line 1, column 1 of 2");
	ui_set_text_color(app.lines, ui_theme.dim);

	struct ui_widget *help = ui_label(root, "Tab moves between controls; Space and Enter press them.");
	ui_set_text_color(help, ui_theme.dim);
	ui_set_timer(app.ui, 100, tick, &app);
	ui_run(app.ui);
	ui_free(app.ui);
	return 0;
}
