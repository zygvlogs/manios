/* greet -- the smallest graphical program: a label and a button that
 * counts clicks. It is the first example in docs/gui.md. */
#include <stdio.h>
#include <ui.h>

struct app {
	struct ui_widget *label;
	int clicks;
};

static void clicked(struct ui_widget *button, void *arg)
{
	struct app *app = arg;
	char text[32];
	(void)button;
	app->clicks++;
	snprintf(text, sizeof(text), "clicked %d time%s", app->clicks, app->clicks == 1 ? "" : "s");
	ui_set_text(app->label, text);
}

int main(void)
{
	static struct app app;
	struct ui *ui = ui_open("Greet", 240, 100);
	if (!ui) {
		fprintf(stderr, "greet: no window (is ManiDE running?)\n");
		return 1;
	}
	app.label = ui_label(ui_root(ui), "Hello, ManiOS");
	ui_button(ui_root(ui), "Click me", clicked, &app);
	ui_run(ui); /* until the window is closed */
	ui_free(ui);
	return 0;
}
