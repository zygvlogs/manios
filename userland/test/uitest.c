/* The widget toolkit's conformance test (M23, docs/gui.md). libui needs
 * no desktop to be tested: a `ui_new` ui draws on a canvas of its own and
 * is fed events by hand, as ManiDE would send them. This checks layout
 * (boxes, weights, shrinking, grids, hidden widgets), what each widget
 * does with keys and the mouse, the focus, the message over the window,
 * the program's own widgets, resizing, and the pixels that result.
 * Run it from any shell:  /boot/test/uitest */
#include <manios.h>
#include <stdio.h>
#include <string.h>
#include <ui.h>
#include <zkt_abi.h>

static int checks, failures;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failures++;
		printf("uitest: FAIL: %s\n", what);
	}
}

static bool rect_is(struct gfx_rect r, int x, int y, int w, int h)
{
	return r.x == x && r.y == y && r.w == w && r.h == h;
}

/* --- events, as the desktop would send them --- */

static void key(struct ui *ui, int k)
{
	struct win_event e = { .type = WIN_KEY, .key = k };
	ui_handle(ui, &e);
	ui_update(ui);
}

static void type(struct ui *ui, const char *s)
{
	for (; *s; s++) {
		key(ui, *s);
	}
}

static void mouse(struct ui *ui, int x, int y, int buttons)
{
	struct win_event e = { .type = WIN_MOUSE, .x = x, .y = y, .buttons = buttons };
	ui_handle(ui, &e);
	ui_update(ui);
}

static void click(struct ui *ui, int x, int y)
{
	mouse(ui, x, y, 1);
	mouse(ui, x, y, 0);
}

static void click_on(struct ui *ui, const struct ui_widget *w)
{
	click(ui, w->r.x + w->r.w / 2, w->r.y + w->r.h / 2);
}

static gfx_color pixel(struct ui *ui, int x, int y)
{
	return gfx_get(ui_canvas_of(ui), x, y);
}

static void bump(struct ui_widget *w, void *arg)
{
	(void)w;
	(*(int *)arg)++;
}

/* --- layout --- */

static void test_boxes(void)
{
	struct ui *ui = ui_new(200, 160);
	struct ui_widget *root = ui_root(ui);
	struct ui_widget *label = ui_label(root, "Hello");
	struct ui_widget *button = ui_button(root, "Go", NULL, NULL);
	struct ui_widget *entry = ui_entry(root, "", 10, NULL, NULL);
	ui_update(ui);
	check(rect_is(root->r, 0, 0, 200, 160), "the root fills the window");
	check(rect_is(label->r, 6, 6, 188, 11), "a label: the root's padding, its width across, its text's height");
	check(rect_is(button->r, 6, 23, 188, 19), "a button: text height plus 2 x 4, 6 below the label");
	check(entry->r.y == 48 && entry->r.h == 19, "an entry follows with the spacing between");

	ui_set_expand(entry, 1);
	ui_update(ui);
	check(entry->r.y + entry->r.h == 154, "an expanding child takes the room left, to the padding");
	check(button->r.h == 19, "the others keep their size");

	/* Two expanding children share the spare room by weight. */
	struct ui *row_ui = ui_new(200, 40);
	struct ui_widget *row = ui_hbox(ui_root(row_ui), 6);
	struct ui_widget *a = ui_spacer(row), *b = ui_spacer(row), *c = ui_spacer(row);
	ui_set_size(a, 20, 10);
	ui_set_size(b, 20, 10);
	ui_set_size(c, 20, 10);
	ui_set_expand(a, 1);
	ui_set_expand(b, 3);
	ui_set_expand(c, 0);
	ui_update(row_ui);
	check(a->r.w == 49 && b->r.w == 107 && c->r.w == 20, "spare room shared 1:3 between expanding widgets");
	check(a->r.x == 6 && b->r.x == 61 && c->r.x == 174, "...and each placed after the one before, 6 apart");
	check(c->r.x + c->r.w == 194, "the row ends at the padding");

	/* Too small: the expanding ones give room back first, last first. */
	struct win_event small = { .type = WIN_RESIZE, .width = 60, .height = 40 };
	ui_handle(row_ui, &small);
	ui_update(row_ui);
	check(c->r.w == 20 && b->r.w == 0 && a->r.w == 16, "a box too small takes room from its expanding children first");
	check(ui_width(row_ui) == 60 && row->r.w == 48, "resizing relaid out the tree");
	check(ui_canvas_of(row_ui)->width == 60, "...and the canvas has the new size");

	/* A grid: equal cells, the remainder shared out. */
	struct ui *grid_ui = ui_new(110, 100);
	ui_set_padding(ui_root(grid_ui), 0);
	struct ui_widget *grid = ui_grid(ui_root(grid_ui), 3, 5);
	struct ui_widget *cell[7];
	for (int i = 0; i < 7; i++) {
		cell[i] = ui_spacer(grid);
		ui_set_size(cell[i], 1, 1);
	}
	ui_set_expand(grid, 1);
	ui_update(grid_ui);
	check(rect_is(cell[0]->r, 0, 0, 33, 30), "grid: the first cell");
	check(rect_is(cell[1]->r, 38, 0, 33, 30) && rect_is(cell[2]->r, 76, 0, 34, 30),
	      "grid: cells differ by at most a pixel and fill the width");
	check(rect_is(cell[4]->r, 38, 35, 33, 30), "grid: row-major, the second row");
	check(rect_is(cell[6]->r, 0, 70, 33, 30), "grid: the last row");
	ui_set_visible(cell[1], false);
	ui_update(grid_ui);
	check(rect_is(cell[2]->r, 38, 0, 33, 47), "a hidden widget leaves its place to the next (6 left: 2 rows)");
	ui_free(grid_ui);
	ui_free(row_ui);
	ui_free(ui);
}

/* --- buttons, the focus --- */

static void test_buttons(void)
{
	struct ui *ui = ui_new(200, 160);
	struct ui_widget *root = ui_root(ui);
	int clicks = 0, entered = 0;
	struct ui_widget *button = ui_button(root, "Go", bump, &clicks);
	struct ui_widget *entry = ui_entry(root, "", 10, bump, &entered);
	ui_update(ui);
	check(ui_has_focus(button), "the first widget that takes the focus has it");
	click_on(ui, button);
	check(clicks == 1, "a click on a button runs its function");
	mouse(ui, button->r.x + 5, button->r.y + 5, 1);
	mouse(ui, button->r.x + 5, button->r.y + 5 + 300, 1); /* dragged away */
	mouse(ui, button->r.x + 5, button->r.y + 5 + 300, 0);
	check(clicks == 1, "pressed, dragged off and released: no click");
	mouse(ui, button->r.x + 5, button->r.y + 5, 1);
	mouse(ui, button->r.x + 5, button->r.y + 5 + 300, 1);
	mouse(ui, button->r.x + 6, button->r.y + 6, 1); /* ... and back */
	mouse(ui, button->r.x + 6, button->r.y + 6, 0);
	check(clicks == 2, "pressed, dragged off and back, released: a click");
	key(ui, ' ');
	key(ui, '\n');
	check(clicks == 4, "Space and Enter press the focused button");
	ui_set_enabled(button, false);
	click_on(ui, button);
	key(ui, ' ');
	check(clicks == 4, "a disabled button does nothing");
	check(ui_has_focus(entry), "disabling the focused widget moves the focus on");
	ui_set_enabled(button, true);

	key(ui, '\t');
	check(ui_has_focus(button), "Tab goes on to the next widget, round to the first");
	click_on(ui, entry);
	check(ui_has_focus(entry), "a click on an entry focuses it");
	type(ui, "ab\n");
	check(entered == 1, "Enter in an entry runs its function");

	/* The pixels: the button's edge shows the focus. */
	click_on(ui, button);
	check(pixel(ui, button->r.x, button->r.y + 5) == ui_theme.focus, "a focused widget's edge is cyan");
	check(pixel(ui, entry->r.x, entry->r.y + 5) == ui_theme.edge, "an unfocused one's is grey");
	struct win_event lost = { .type = WIN_FOCUS, .focused = 0 };
	ui_handle(ui, &lost);
	ui_update(ui);
	check(pixel(ui, button->r.x, button->r.y + 5) == ui_theme.edge, "...and grey when the window loses the focus");
	struct win_event got = { .type = WIN_FOCUS, .focused = 1 };
	ui_handle(ui, &got);
	ui_update(ui);
	check(pixel(ui, button->r.x + 3, button->r.y + 3) == ui_theme.panel, "a button's body is the panel colour");
	check(pixel(ui, 0, 0) == ui_theme.bg, "the window's own background is the theme's");

	ui_destroy(button);
	ui_update(ui);
	check(ui_has_focus(entry), "destroying the focused widget leaves the focus to the first that takes it");
	ui_free(ui);
}

static bool eat_x(struct ui *ui, int k, void *arg)
{
	(void)ui;
	(*(int *)arg)++;
	return k == 'x';
}

static bool count_keys(struct ui *ui, int k, void *arg)
{
	(void)ui;
	(void)k;
	(*(int *)arg)++;
	return false;
}

static bool refuse_close(struct ui *ui, void *arg)
{
	(void)ui;
	(*(int *)arg)++;
	return false;
}

static void test_hooks(void)
{
	struct ui *ui = ui_new(120, 60);
	int seen = 0, asked = 0;
	struct ui_widget *entry = ui_entry(ui_root(ui), "", 8, NULL, NULL);
	ui_on_key(ui, eat_x, &seen);
	ui_update(ui);
	type(ui, "axb");
	check(seen == 3 && !strcmp(ui_get_text(entry), "ab"), "a key hook sees every key and may use one up");
	ui_on_close(ui, refuse_close, &asked);
	struct win_event close = { .type = WIN_CLOSE };
	ui_handle(ui, &close);
	check(asked == 1 && !ui_quitting(ui), "a close hook can refuse");
	ui_on_close(ui, NULL, NULL);
	check(!ui_handle(ui, &close) && ui_quitting(ui), "...and without one the window closes");
	ui_free(ui);
}

/* --- widgets --- */

static void test_entry(void)
{
	struct ui *ui = ui_new(200, 60);
	int changes = 0;
	struct ui_widget *entry = ui_entry(ui_root(ui), "", 10, NULL, NULL);
	ui_on_change(entry, bump, &changes);
	ui_update(ui);
	type(ui, "abc");
	check(!strcmp(ui_get_text(entry), "abc") && changes == 3, "typing adds characters, and says so");
	key(ui, ZKT_KEY_LEFT);
	key(ui, ZKT_KEY_LEFT);
	type(ui, "X");
	check(!strcmp(ui_get_text(entry), "aXbc"), "Left moves the caret; typing inserts there");
	key(ui, ZKT_KEY_HOME);
	type(ui, "Y");
	key(ui, ZKT_KEY_END);
	key(ui, '\b');
	check(!strcmp(ui_get_text(entry), "YaXb"), "Home, End and Backspace");
	key(ui, ZKT_KEY_HOME);
	key(ui, ZKT_KEY_DELETE);
	check(!strcmp(ui_get_text(entry), "aXb"), "Delete removes the character after the caret");
	key(ui, 0x01); /* ^A */
	key(ui, ZKT_KEY_RIGHT);
	key(ui, 0x0B); /* ^K */
	check(!strcmp(ui_get_text(entry), "a"), "^K kills to the end of the line");
	key(ui, 0x15); /* ^U */
	check(!strcmp(ui_get_text(entry), ""), "^U clears it");
	type(ui, "0123456789ABC");
	check(!strcmp(ui_get_text(entry), "0123456789"), "an entry takes no more than its maximum");
	click(ui, entry->r.x + 4 + 3 * GFX_CELL_W + 1, entry->r.y + 8);
	key(ui, '\b');
	check(!strcmp(ui_get_text(entry), "013456789"), "a click puts the caret between characters 3 and 4");
	ui_set_text(entry, "set");
	check(!strcmp(ui_get_text(entry), "set"), "ui_set_text replaces the contents");
	key(ui, '!');
	check(!strcmp(ui_get_text(entry), "set!"), "...the caret at its end");
	ui_free(ui);

	/* A line longer than the entry scrolls to keep the caret in view. */
	ui = ui_new(200, 60);
	entry = ui_entry(ui_root(ui), "", 40, NULL, NULL);
	ui_update(ui);
	for (int i = 0; i < 35; i++) {
		key(ui, 'a' + i % 26);
	}
	int cols = (entry->r.w - 8) / GFX_CELL_W, ty = entry->r.y + (entry->r.h - GFX_CELL_H) / 2;
	check(cols == 30 && pixel(ui, entry->r.x + 4 + (cols - 1) * GFX_CELL_W, ty + 5) == ui_theme.accent,
	      "the caret stays in view: the last column, once the line outgrows the entry");
	key(ui, ZKT_KEY_HOME);
	check(pixel(ui, entry->r.x + 4, ty + 5) == ui_theme.accent, "...and Home scrolls back to the start");
	ui_free(ui);
}

static void test_checkbox(void)
{
	struct ui *ui = ui_new(200, 60);
	int toggles = 0;
	struct ui_widget *box = ui_checkbox(ui_root(ui), "Option", false, bump, &toggles);
	ui_update(ui);
	check(!ui_checked(box), "a checkbox starts as it was made");
	click_on(ui, box);
	check(ui_checked(box) && toggles == 1, "a click checks it, and runs its function");
	key(ui, ' ');
	check(!ui_checked(box) && toggles == 2, "Space unchecks it");
	ui_set_checked(box, true);
	check(ui_checked(box) && toggles == 2, "setting it from the program runs nothing");
	ui_set_enabled(box, false);
	click_on(ui, box);
	check(ui_checked(box), "a disabled checkbox ignores the mouse");
	ui_free(ui);
}

static void test_list(void)
{
	struct ui *ui = ui_new(200, 100);
	int activated = 0, changed = 0;
	struct ui_widget *list = ui_list(ui_root(ui), bump, &activated);
	ui_on_change(list, bump, &changed);
	ui_set_expand(list, 1);
	for (int i = 0; i < 30; i++) {
		char text[32];
		snprintf(text, sizeof(text), "row %d\t%d", i, i * 10);
		ui_list_add(list, text, 0, (void *)(long)i);
	}
	ui_update(ui);
	check(ui_list_count(list) == 30 && ui_list_selected(list) == -1, "30 rows, none selected");
	key(ui, ZKT_KEY_DOWN);
	check(ui_list_selected(list) == 0 && changed == 1, "Down selects the first row, and says so");
	key(ui, ZKT_KEY_DOWN);
	key(ui, ZKT_KEY_DOWN);
	check(ui_list_selected(list) == 2, "Down goes on");
	key(ui, ZKT_KEY_END);
	check(ui_list_selected(list) == 29, "End selects the last row");
	key(ui, ZKT_KEY_UP);
	key(ui, ZKT_KEY_HOME);
	check(ui_list_selected(list) == 0, "Home the first");
	key(ui, ZKT_KEY_PGDN);
	check(ui_list_selected(list) > 3, "PgDn goes a page");
	key(ui, '\n');
	check(activated == 1, "Enter activates the selected row");
	check(!strcmp(ui_list_text(list, 3), "row 3\t30") && (long)ui_list_data(list, 3) == 3,
	      "a row keeps its text and its data");

	/* Rows by mouse: row k is at 1 + k * ROW_H inside the border. */
	ui_list_select(list, 0);
	int y = list->r.y + 1 + 2 * 13 + 4;
	click(ui, list->r.x + 20, y);
	check(ui_list_selected(list) == 2, "a click selects the row under it");
	sleep_ms(600);
	click(ui, list->r.x + 20, y);
	check(activated == 1, "two clicks far apart are not a double click");
	click(ui, list->r.x + 20, y);
	check(activated == 2, "two clicks close together are: the row is activated");
	check(pixel(ui, list->r.x + 40, y) == ui_theme.select, "the selected row is drawn in the selection colour");
	check(pixel(ui, list->r.x + 40, y + 13) == ui_theme.field, "...and the row below it is not");
	ui_list_clear(list);
	check(ui_list_count(list) == 0 && ui_list_selected(list) == -1, "clearing empties it");
	ui_free(ui);
}

static void test_text(void)
{
	struct ui *ui = ui_new(200, 120);
	struct ui_widget *text = ui_text(ui_root(ui), false);
	ui_set_expand(text, 1);
	ui_update(ui);
	type(ui, "ab\ncd");
	key(ui, '\n');
	type(ui, "ef");
	check(!strcmp(ui_get_text(text), "ab\ncd\nef") && ui_text_lines(text) == 3, "typing, with Enter, makes lines");
	check(ui_text_line(text) == 2 && ui_text_column(text) == 2, "the caret is where the typing left it");
	key(ui, ZKT_KEY_UP);
	key(ui, ZKT_KEY_UP);
	check(ui_text_line(text) == 0 && ui_text_column(text) == 2, "Up goes by lines, keeping the column");
	key(ui, ZKT_KEY_DOWN);
	key(ui, ZKT_KEY_HOME);
	key(ui, '\b');
	check(!strcmp(ui_get_text(text), "abcd\nef") && ui_text_lines(text) == 2,
	      "Backspace at a line's start joins it to the one before");
	key(ui, ZKT_KEY_END);
	key(ui, ZKT_KEY_DELETE);
	check(!strcmp(ui_get_text(text), "abcdef"), "Delete at a line's end joins the next");
	key(ui, 0x0B); /* ^K at the end of the line */
	key(ui, ZKT_KEY_HOME);
	key(ui, 0x0B);
	check(!strcmp(ui_get_text(text), ""), "^K kills to the end of the line");
	ui_set_text(text, "x\n\tz\n");
	key(ui, ZKT_KEY_DOWN);
	key(ui, ZKT_KEY_END);
	check(ui_text_column(text) == 5, "a Tab stands for spaces up to a multiple of 4");
	click(ui, text->r.x + 1 + 3 + 4 * GFX_CELL_W + 2, text->r.y + 2 + 11 + 3);
	check(ui_text_line(text) == 1 && ui_text_column(text) == 4, "a click puts the caret on the character under it");
	key(ui, '\t');
	check(!strcmp(ui_get_text(text), "x\n\tz\n"), "Tab is not typed into the text (it moves the focus)");

	/* Read-only text scrolls; the caret does not edit. */
	struct ui *view = ui_new(200, 80);
	struct ui_widget *ro = ui_text(ui_root(view), true);
	ui_set_expand(ro, 1);
	char many[1200] = "";
	for (int i = 0; i < 100; i++) {
		char line[16];
		snprintf(line, sizeof(line), "line %d\n", i);
		strlcat(many, line, sizeof(many));
	}
	ui_set_text(ro, many);
	ui_text_numbers(ro, true);
	ui_update(view);
	check(ui_text_lines(ro) == 100, "a read-only text ending in a newline has no empty last line");
	check(ui_text_top(ro) == 0, "it starts at the top");
	key(view, ZKT_KEY_DOWN);
	key(view, ZKT_KEY_DOWN);
	check(ui_text_top(ro) == 2, "Down scrolls a line");
	key(view, ZKT_KEY_PGDN);
	check(ui_text_top(ro) > 5, "PgDn a page");
	key(view, ZKT_KEY_END);
	int last = ui_text_top(ro);
	key(view, ZKT_KEY_DOWN);
	check(last > 90 && ui_text_top(ro) == last, "End shows the last lines and Down goes no further");
	key(view, ZKT_KEY_HOME);
	check(ui_text_top(ro) == 0, "Home goes back to the top");
	key(view, 'x');
	check(ui_text_lines(ro) == 100 && !strncmp(ui_get_text(ro), "line 0\n", 7), "typing changes nothing");
	/* The scroll bar: a click in the track below the thumb pages. */
	click(view, ro->r.x + ro->r.w - 5, ro->r.y + ro->r.h - 4);
	check(ui_text_top(ro) > 0, "a click in the scroll bar's track below the thumb pages down");
	/* Dragging the thumb to the bottom shows the end. */
	ui_text_goto(ro, 0);
	ui_update(view);
	int bar_x = ro->r.x + ro->r.w - 5;
	mouse(view, bar_x, ro->r.y + 4, 1);
	mouse(view, bar_x, ro->r.y + ro->r.h + 20, 1);
	mouse(view, bar_x, ro->r.y + ro->r.h + 20, 0);
	check(ui_text_top(ro) > 90, "dragging the thumb to the bottom shows the end");
	ui_free(view);
	ui_free(ui);
}

static void test_progress_and_labels(void)
{
	struct ui *ui = ui_new(160, 100);
	struct ui_widget *root = ui_root(ui);
	struct ui_widget *label = ui_label(root, "one\ntwo");
	struct ui_widget *bar = ui_progress(root);
	ui_set_size(bar, 100, 0);
	ui_update(ui);
	check(label->r.h == 22, "a two-line label is two lines high");
	ui_set_text(label, "just one");
	ui_update(ui);
	check(label->r.h == 11, "setting a label's text sets its size");
	ui_set_scale(label, 2);
	ui_update(ui);
	check(label->r.h == 22 && label->want_w == 8 * 12, "a label at scale 2 is twice the size each way");
	ui_set_progress(bar, 50, 100);
	ui_update(ui);
	int inner = bar->r.w - 2, half = inner / 2;
	check(pixel(ui, bar->r.x + half, bar->r.y + 5) == ui_theme.accent
	      && pixel(ui, bar->r.x + 1 + half + 2, bar->r.y + 5) == ui_theme.field,
	      "a progress bar is filled as far as its value");
	ui_set_progress(bar, 150, 100);
	ui_update(ui);
	check(pixel(ui, bar->r.x + bar->r.w - 2, bar->r.y + 5) == ui_theme.accent, "a value past the maximum fills it, no more");
	ui_set_colors(label, GFX_RGB(1, 2, 3), 0);
	ui_update(ui);
	check(pixel(ui, label->r.x + 1, label->r.y + 1) == GFX_RGB(1, 2, 3), "a widget's own background colour");
	ui_free(ui);
}

/* --- the message --- */

static void test_message(void)
{
	struct ui *ui = ui_new(200, 120);
	int clicks = 0, hooked = 0;
	struct ui_widget *button = ui_button(ui_root(ui), "Under", bump, &clicks);
	ui_set_expand(button, 1);
	ui_on_key(ui, count_keys, &hooked);
	ui_update(ui);
	ui_message(ui, "Something happened");
	ui_update(ui);
	check(pixel(ui, 1, 1) != ui_theme.bg, "a message dims what is under it");
	click(ui, button->r.x + 3, button->r.y + 3);
	key(ui, 'q');
	key(ui, '\t');
	check(clicks == 0 && pixel(ui, 1, 1) != ui_theme.bg, "while a message shows, nothing under it works, and it stays");
	check(hooked == 0, "...and the program's key hook is not called with its keys");
	key(ui, '\n');
	check(pixel(ui, 1, 1) == ui_theme.bg && hooked == 0, "Enter closes it");
	key(ui, 'k');
	click_on(ui, button);
	check(clicks == 1 && hooked == 1, "the window works again, hook and all");
	ui_message(ui, "Again");
	ui_update(ui);
	key(ui, 0x1B);
	check(pixel(ui, 1, 1) == ui_theme.bg, "Escape closes it too");
	key(ui, '\n');
	check(clicks == 2 && ui_has_focus(button), "the focus went back to where it was");
	/* "Once more" (54 pixels) in a message of 78 x 64 at (61, 28): its OK
	 * button is the lower part, x 73..127, y 61..80. */
	ui_message(ui, "Once more");
	ui_update(ui);
	click(ui, 100, 40);
	check(pixel(ui, 1, 1) != ui_theme.bg, "a click on the message's text leaves it");
	click(ui, 100, 70);
	check(pixel(ui, 1, 1) == ui_theme.bg, "a click on its OK button closes it");
	click_on(ui, button);
	check(clicks == 3, "and the window works again");
	ui_free(ui);

	/* A widget destroyed while a message is over it (a timer can do that)
	 * can't be given the focus back. */
	ui = ui_new(200, 120);
	struct ui_widget *gone = ui_button(ui_root(ui), "Gone", NULL, NULL);
	struct ui_widget *stays = ui_entry(ui_root(ui), "", 8, NULL, NULL);
	ui_update(ui);
	check(ui_has_focus(gone), "the first widget has the focus");
	ui_message(ui, "Hold on");
	ui_update(ui);
	ui_destroy(gone);
	key(ui, '\n');
	check(ui_has_focus(stays), "the focus, when a message closes, is not a destroyed widget: the next that takes it");
	type(ui, "ok");
	check(!strcmp(ui_get_text(stays), "ok"), "and it takes keys");
	ui_free(ui);
}

/* --- the program's own widgets --- */

struct custom_log {
	int painted, resized, keys, presses, drags, releases, moves;
	int last_x, last_y, last_buttons;
	struct gfx_rect painted_at;
};

static void c_paint(struct ui_widget *w, struct gfx_canvas *c, void *arg)
{
	struct custom_log *log = arg;
	log->painted++;
	log->painted_at = w->r;
	gfx_fill(c, w->r, GFX_RGB(9, 8, 7));
}

static bool c_key(struct ui_widget *w, int k, void *arg)
{
	(void)w;
	((struct custom_log *)arg)->keys++;
	return k == 'a';
}

static void c_mouse(struct ui_widget *w, enum ui_mouse what, int x, int y, int buttons, void *arg)
{
	(void)w;
	struct custom_log *log = arg;
	log->last_x = x;
	log->last_y = y;
	log->last_buttons = buttons;
	log->presses += what == UI_PRESS;
	log->drags += what == UI_DRAG;
	log->releases += what == UI_RELEASE;
	log->moves += what == UI_MOVE;
}

static void c_resized(struct ui_widget *w, void *arg)
{
	(void)w;
	((struct custom_log *)arg)->resized++;
}

static void test_custom(void)
{
	struct ui *ui = ui_new(150, 100);
	struct custom_log log = { 0 };
	struct ui_custom ops = { c_paint, c_key, c_mouse, c_resized, true };
	struct ui_widget *w = ui_custom(ui_root(ui), &ops, &log);
	ui_set_expand(w, 1);
	ui_update(ui);
	check(log.painted >= 1 && rect_is(log.painted_at, 6, 6, 138, 88), "a custom widget is painted where it is laid out");
	check(pixel(ui, 20, 20) == GFX_RGB(9, 8, 7), "...into the window's canvas");
	check(log.resized == 1, "it is told its size once laid out");
	check(ui_has_focus(w), "a focusable one takes the focus");
	type(ui, "ab");
	check(log.keys == 2, "it gets the keys");
	mouse(ui, 16, 26, 0);
	check(log.moves == 1 && log.last_x == 10 && log.last_y == 20, "the pointer moving over it, relative to it");
	mouse(ui, 16, 26, 1);
	mouse(ui, 300, 300, 3);
	mouse(ui, 300, 300, 0);
	check(log.presses == 1 && log.drags >= 1 && log.releases == 1, "press, drag and release, even once the pointer left it");
	check(log.last_buttons == 0, "the release has no buttons");
	struct win_event big = { .type = WIN_RESIZE, .width = 250, .height = 100 };
	ui_handle(ui, &big);
	ui_update(ui);
	check(log.resized == 2, "a new size tells it again");
	ui_free(ui);
}

int main(void)
{
	test_boxes();
	test_buttons();
	test_hooks();
	test_entry();
	test_checkbox();
	test_list();
	test_text();
	test_progress_and_labels();
	test_message();
	test_custom();
	if (failures) {
		printf("uitest: %d of %d checks failed\n", failures, checks);
		return 1;
	}
	printf("uitest: all %d checks passed\n", checks);
	return 0;
}
