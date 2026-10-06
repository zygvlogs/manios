/* paint -- a sketchpad. Draw with the left mouse button in the colour
 * chosen from the palette, erase with the right one; - and + (or [ and ])
 * change the brush, Clear (or c) wipes the page. The picture lives in
 * memory only: paint does not save yet.
 *
 * It is also libui's example of a program drawing and handling the mouse
 * itself, in two `ui_custom` widgets (docs/gui.md). */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ui.h>

#define SWATCH 20
#define PITCH 24 /* a swatch and the gap after it */
#define COLUMNS 2
#define COLORS 16
#define MIN_BRUSH 1
#define MAX_BRUSH 24
#define PAPER GFX_RGB(0xFF, 0xFF, 0xFF)

static const gfx_color PALETTE[COLORS] = {
	GFX_RGB(0x00, 0x00, 0x00), GFX_RGB(0x55, 0x55, 0x55), GFX_RGB(0xAA, 0xAA, 0xAA), GFX_RGB(0xFF, 0xFF, 0xFF),
	GFX_RGB(0xCC, 0x22, 0x22), GFX_RGB(0xE0, 0x7A, 0x2E), GFX_RGB(0xE8, 0xD0, 0x30), GFX_RGB(0x6A, 0xB8, 0x3A),
	GFX_RGB(0x1E, 0x7A, 0x3C), GFX_RGB(0x3F, 0xC8, 0xD8), GFX_RGB(0x30, 0x6A, 0xD0), GFX_RGB(0x20, 0x20, 0x90),
	GFX_RGB(0x80, 0x40, 0xB0), GFX_RGB(0xD0, 0x50, 0xA0), GFX_RGB(0x8B, 0x5A, 0x2B), GFX_RGB(0xF0, 0xA0, 0xA0),
};

struct app {
	struct ui *ui;
	struct ui_widget *palette, *canvas, *size_label;
	struct gfx_canvas *picture; /* the page; at least as big as the widget has been */
	gfx_color color;
	int brush;
	bool drawing; /* a stroke is under way: last_x, last_y is where it was */
	int last_x, last_y;
};

static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }
static int swatch_x(int i) { return i % COLUMNS * PITCH; }
static int swatch_y(int i) { return i / COLUMNS * PITCH; }

static void palette_paint(struct ui_widget *w, struct gfx_canvas *c, void *arg)
{
	struct app *app = arg;
	for (int i = 0; i < COLORS; i++) {
		struct gfx_rect r = { w->r.x + swatch_x(i), w->r.y + swatch_y(i), SWATCH, SWATCH };
		gfx_fill(c, r, PALETTE[i]);
		bool chosen = PALETTE[i] == app->color;
		gfx_outline(c, r, chosen ? ui_theme.focus : ui_theme.edge);
		if (chosen) {
			gfx_outline(c, (struct gfx_rect){ r.x + 1, r.y + 1, r.w - 2, r.h - 2 }, ui_theme.accent);
		}
	}
}

static void palette_mouse(struct ui_widget *w, enum ui_mouse what, int x, int y, int buttons, void *arg)
{
	struct app *app = arg;
	(void)buttons;
	if (what != UI_PRESS || x < 0 || y < 0) {
		return;
	}
	int col = x / PITCH, row = y / PITCH, i = row * COLUMNS + col;
	if (col < COLUMNS && x % PITCH < SWATCH && y % PITCH < SWATCH && i < COLORS) {
		app->color = PALETTE[i];
		ui_redraw(w);
	}
}

/* The page must cover the widget: bigger, it grows and keeps its picture. */
static void ensure_page(struct app *app, int width, int height)
{
	struct gfx_canvas *old = app->picture;
	if (old && old->width >= width && old->height >= height) {
		return;
	}
	struct gfx_canvas *page = gfx_canvas_new(imax(old ? old->width : 0, width),
	                                         imax(old ? old->height : 0, height));
	if (!page) {
		return;
	}
	gfx_fill(page, (struct gfx_rect){ 0, 0, page->width, page->height }, PAPER);
	if (old) {
		gfx_blit(page, 0, 0, old, (struct gfx_rect){ 0, 0, old->width, old->height });
		gfx_canvas_free(old);
	}
	app->picture = page;
}

static void canvas_resized(struct ui_widget *w, void *arg)
{
	ensure_page(arg, w->r.w, w->r.h);
}

static void canvas_paint(struct ui_widget *w, struct gfx_canvas *c, void *arg)
{
	struct app *app = arg;
	gfx_fill(c, w->r, PAPER);
	if (app->picture) {
		gfx_blit(c, w->r.x, w->r.y, app->picture,
		         (struct gfx_rect){ 0, 0, imin(w->r.w, app->picture->width),
		                            imin(w->r.h, app->picture->height) });
	}
}

/* A brush mark from (x0, y0) to (x1, y1) in `color`; the mark is a
 * circle stamped at every pixel step along the way. */
static void stroke(struct app *app, int x0, int y0, int x1, int y1, gfx_color color)
{
	int dx = x1 - x0, dy = y1 - y0, steps = imax(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy);
	int radius = app->brush / 2;
	for (int i = 0; i <= steps; i++) {
		int x = steps ? x0 + dx * i / steps : x0, y = steps ? y0 + dy * i / steps : y0;
		gfx_fill_circle(app->picture, x, y, radius, color);
	}
	/* Only what was marked needs drawing again. */
	int left = imin(x0, x1) - radius, top = imin(y0, y1) - radius;
	ui_redraw_rect(app->canvas, (struct gfx_rect){ app->canvas->r.x + left, app->canvas->r.y + top,
	                                               imax(x0, x1) + radius + 1 - left,
	                                               imax(y0, y1) + radius + 1 - top });
}

static void canvas_mouse(struct ui_widget *w, enum ui_mouse what, int x, int y, int buttons, void *arg)
{
	struct app *app = arg;
	(void)w;
	if (what == UI_RELEASE || !app->picture) {
		app->drawing = false;
		return;
	}
	if (what == UI_PRESS || what == UI_DRAG) {
		if (!(buttons & 3)) {
			return;
		}
		gfx_color color = buttons & 1 ? app->color : PAPER; /* the right button erases */
		if (what == UI_PRESS || !app->drawing) {
			app->last_x = x;
			app->last_y = y;
		}
		stroke(app, app->last_x, app->last_y, x, y, color);
		app->drawing = true;
		app->last_x = x;
		app->last_y = y;
	}
}

static void show_brush(struct app *app)
{
	char text[16];
	snprintf(text, sizeof(text), "Brush %d", app->brush);
	ui_set_text(app->size_label, text);
}

static void set_brush(struct app *app, int brush)
{
	app->brush = brush < MIN_BRUSH ? MIN_BRUSH : brush > MAX_BRUSH ? MAX_BRUSH : brush;
	show_brush(app);
}

static void clear_page(struct app *app)
{
	if (app->picture) {
		gfx_fill(app->picture, (struct gfx_rect){ 0, 0, app->picture->width, app->picture->height }, PAPER);
	}
	ui_redraw(app->canvas);
}

/* One step a time up to 4, then two. */
static void on_smaller(struct ui_widget *w, void *arg)
{
	struct app *app = arg;
	(void)w;
	set_brush(app, app->brush - (app->brush > 4 ? 2 : 1));
}

static void on_bigger(struct ui_widget *w, void *arg)
{
	struct app *app = arg;
	(void)w;
	set_brush(app, app->brush + (app->brush >= 4 ? 2 : 1));
}

static void on_clear(struct ui_widget *w, void *arg)
{
	(void)w;
	clear_page(arg);
}

static bool typed(struct ui *ui, int key, void *arg)
{
	struct app *app = arg;
	(void)ui;
	if (key == '[' || key == '-') {
		on_smaller(NULL, app);
	} else if (key == ']' || key == '+' || key == '=') {
		on_bigger(NULL, app);
	} else if (key == 'c' || key == 'C') {
		clear_page(app);
	} else {
		return false;
	}
	return true;
}

int main(void)
{
	static struct app app;
	app.ui = ui_open("Paint", 480, 340);
	if (!app.ui) {
		fprintf(stderr, "paint: no window: %s\n", strerror(errno));
		return 1;
	}
	app.color = PALETTE[0];
	struct ui_widget *main_row = ui_hbox(ui_root(app.ui), 8);
	ui_set_expand(main_row, 1);
	struct ui_widget *tools = ui_vbox(main_row, 6);
	struct ui_custom palette = { palette_paint, NULL, palette_mouse, NULL, false };
	app.palette = ui_custom(tools, &palette, &app);
	ui_set_size(app.palette, COLUMNS * PITCH - (PITCH - SWATCH), COLORS / COLUMNS * PITCH - (PITCH - SWATCH));
	app.size_label = ui_label(tools, "");
	ui_set_align(app.size_label, UI_CENTER);
	struct ui_widget *sizes = ui_hbox(tools, 4);
	struct ui_widget *smaller = ui_button(sizes, "-", on_smaller, &app);
	struct ui_widget *bigger = ui_button(sizes, "+", on_bigger, &app);
	ui_set_expand(smaller, 1);
	ui_set_expand(bigger, 1);
	ui_button(tools, "Clear", on_clear, &app);
	ui_spacer(tools);
	struct ui_custom page = { canvas_paint, NULL, canvas_mouse, canvas_resized, false };
	app.canvas = ui_custom(main_row, &page, &app);
	ui_set_expand(app.canvas, 1);
	ui_set_size(app.canvas, 120, 120);
	app.brush = 4;
	show_brush(&app);
	ui_on_key(app.ui, typed, &app);
	ui_run(app.ui);
	ui_free(app.ui);
	gfx_canvas_free(app.picture);
	return 0;
}
