/* The simple widgets (ui.h): label, button, checkbox, entry, progress bar,
 * separator, spacer, and the widget the program draws itself. */
#include "ui_priv.h"
#include <stdlib.h>
#include <string.h>
#include <zkt_abi.h>

#define CHECK_SIZE 11
#define LINE_MAX_CHARS 255

static void fire(struct ui_widget *w)
{
	if (w->activate) {
		w->activate(w, w->activate_arg);
	}
}

static void fire_change(struct ui_widget *w)
{
	if (w->change) {
		w->change(w, w->change_arg);
	}
}

static bool inside(const struct ui_widget *w, int x, int y)
{
	return x >= 0 && y >= 0 && x < w->r.w && y < w->r.h;
}

/* --- text: label, button, checkbox --- */

struct textw {
	struct ui_widget w;
	char *text;
	int scale;
	enum ui_align align;
	gfx_color color;
	bool down;    /* a button being pressed */
	bool checked; /* a checkbox */
};

static int line_count(const char *s)
{
	int n = 1;
	for (; *s; s++) {
		n += *s == '\n';
	}
	return n;
}

/* Draws s inside r, a line at a time: left, centred or right, and
 * centred vertically. Lines over LINE_MAX_CHARS are cut. */
static void draw_lines(struct gfx_canvas *c, struct gfx_rect r, const char *s, int scale,
                       enum ui_align align, gfx_color color)
{
	int line_h = GFX_CELL_H * scale, y = r.y + (r.h - line_count(s) * line_h) / 2;
	char line[LINE_MAX_CHARS + 1];
	while (*s) {
		size_t n = strcspn(s, "\n");
		size_t take = n < LINE_MAX_CHARS ? n : LINE_MAX_CHARS;
		memcpy(line, s, take);
		line[take] = '\0';
		int width = (int)take * GFX_CELL_W * scale;
		int x = align == UI_CENTER ? r.x + (r.w - width) / 2
		        : align == UI_RIGHT ? r.x + r.w - width : r.x;
		gfx_text(c, x, y, line, color, scale);
		y += line_h;
		s += n + (s[n] == '\n');
	}
}

static void textw_destroy(struct ui_widget *w)
{
	free(((struct textw *)w)->text);
}

static void textw_set_text(struct ui_widget *w, const char *text)
{
	struct textw *t = (struct textw *)w;
	if (strcmp(t->text, text ? text : "") == 0) {
		return;
	}
	int old_w = gfx_text_width(t->text, t->scale), old_lines = line_count(t->text);
	if (!ui_copy_string(&t->text, text)) {
		return;
	}
	/* The same size: only this widget needs drawing again. */
	if (gfx_text_width(t->text, t->scale) == old_w && line_count(t->text) == old_lines) {
		ui_redraw(w);
	} else {
		ui_relayout(w->ui);
	}
}

static const char *textw_get_text(const struct ui_widget *w)
{
	return ((const struct textw *)w)->text;
}

static struct textw *textw_new(struct ui_widget *parent, const struct ui_class *cls, const char *text,
                               unsigned flags)
{
	struct textw *t = parent ? ui_new_widget(parent, cls, sizeof(*t), flags) : NULL;
	if (!t) {
		return NULL;
	}
	t->scale = 1;
	t->color = ui_theme.text;
	if (!ui_copy_string(&t->text, text)) {
		ui_destroy(&t->w);
		return NULL;
	}
	return t;
}

static void label_measure(struct ui_widget *w)
{
	struct textw *t = (struct textw *)w;
	bool boxed = w->bg || w->border;
	w->want_w = gfx_text_width(t->text, t->scale) + (boxed ? 12 : 0);
	w->want_h = line_count(t->text) * GFX_CELL_H * t->scale + (boxed ? 8 : 0);
}

static void label_paint(struct ui_widget *w, struct gfx_canvas *c)
{
	struct textw *t = (struct textw *)w;
	bool boxed = w->bg || w->border;
	struct gfx_rect r = w->r;
	if (boxed) {
		r.x += 6;
		r.w -= 12;
	}
	draw_lines(c, r, t->text, t->scale, t->align,
	           ui_is_enabled(w) ? t->color : ui_theme.dim);
}

static const struct ui_class label_class = {
	.name = "label", .measure = label_measure, .paint = label_paint, .destroy = textw_destroy,
	.set_text = textw_set_text, .get_text = textw_get_text,
};

struct ui_widget *ui_label(struct ui_widget *parent, const char *text)
{
	struct textw *t = textw_new(parent, &label_class, text, 0);
	return t ? &t->w : NULL;
}

static bool is_text_widget(const struct ui_widget *w)
{
	return w && (w->cls == &label_class || w->cls->get_text == textw_get_text);
}

void ui_set_scale(struct ui_widget *w, int scale)
{
	if (is_text_widget(w) && scale >= 1 && scale <= 8) {
		((struct textw *)w)->scale = scale;
		ui_relayout(w->ui);
	}
}

void ui_set_align(struct ui_widget *w, enum ui_align align)
{
	if (is_text_widget(w)) {
		((struct textw *)w)->align = align;
		ui_redraw(w);
	}
}

void ui_set_text_color(struct ui_widget *w, gfx_color color)
{
	if (is_text_widget(w)) {
		((struct textw *)w)->color = color;
		ui_redraw(w);
	}
}

/* --- button --- */

static void button_measure(struct ui_widget *w)
{
	struct textw *t = (struct textw *)w;
	w->want_w = gfx_text_width(t->text, t->scale) + 2 * UI_PAD_X;
	w->want_h = line_count(t->text) * GFX_CELL_H * t->scale + 2 * UI_PAD_Y;
}

static void button_paint(struct ui_widget *w, struct gfx_canvas *c)
{
	struct textw *t = (struct textw *)w;
	bool enabled = ui_is_enabled(w), hot = w->ui->hover == w && enabled;
	gfx_color fill = !enabled ? ui_theme.panel : t->down ? ui_theme.edge
	                 : hot ? ui_theme.panel_hot : ui_theme.panel;
	ui_draw_frame(w, c, fill);
	struct gfx_rect r = w->r;
	if (t->down) {
		r.x++;
		r.y++;
	}
	draw_lines(c, r, t->text, t->scale, UI_CENTER, !enabled ? ui_theme.dim
	           : t->down ? ui_theme.accent : t->color);
}

static bool button_key(struct ui_widget *w, int key)
{
	if (key == ' ' || key == '\n' || key == '\r') {
		fire(w);
		return true;
	}
	return false;
}

static void button_mouse(struct ui_widget *w, enum ui_mouse what, int x, int y, int buttons)
{
	struct textw *t = (struct textw *)w;
	(void)buttons;
	bool was = t->down;
	if (what == UI_PRESS) {
		t->down = true;
	} else if (what == UI_DRAG) {
		t->down = inside(w, x, y);
	} else if (what == UI_RELEASE) {
		bool click = t->down && inside(w, x, y);
		t->down = false;
		ui_redraw(w);
		if (click) {
			fire(w); /* may free the button: nothing after this */
		}
		return;
	}
	if (t->down != was) {
		ui_redraw(w);
	}
}

static const struct ui_class button_class = {
	.name = "button", .measure = button_measure, .paint = button_paint, .key = button_key,
	.mouse = button_mouse, .destroy = textw_destroy, .set_text = textw_set_text,
	.get_text = textw_get_text,
};

struct ui_widget *ui_button(struct ui_widget *parent, const char *text, ui_fn fn, void *arg)
{
	struct textw *t = textw_new(parent, &button_class, text, UI_F_FOCUSABLE);
	if (!t) {
		return NULL;
	}
	t->w.activate = fn;
	t->w.activate_arg = arg;
	return &t->w;
}

/* --- checkbox --- */

static void check_measure(struct ui_widget *w)
{
	struct textw *t = (struct textw *)w;
	w->want_w = CHECK_SIZE + 6 + gfx_text_width(t->text, t->scale);
	w->want_h = ui_imax(CHECK_SIZE, GFX_CELL_H * t->scale) + 4;
}

static void check_paint(struct ui_widget *w, struct gfx_canvas *c)
{
	struct textw *t = (struct textw *)w;
	bool enabled = ui_is_enabled(w);
	struct gfx_rect box = { w->r.x, w->r.y + (w->r.h - CHECK_SIZE) / 2, CHECK_SIZE, CHECK_SIZE };
	gfx_fill(c, box, ui_theme.field);
	gfx_outline(c, box, ui_is_focused(w) ? ui_theme.focus : ui_theme.edge);
	if (t->checked) {
		gfx_color mark = enabled ? ui_theme.accent : ui_theme.dim;
		gfx_line(c, box.x + 2, box.y + 5, box.x + 4, box.y + 8, mark);
		gfx_line(c, box.x + 4, box.y + 8, box.x + 9, box.y + 2, mark);
		gfx_line(c, box.x + 2, box.y + 4, box.x + 4, box.y + 7, mark);
		gfx_line(c, box.x + 4, box.y + 7, box.x + 9, box.y + 1, mark);
	}
	struct gfx_rect r = { w->r.x + CHECK_SIZE + 6, w->r.y, w->r.w - CHECK_SIZE - 6, w->r.h };
	draw_lines(c, r, t->text, t->scale, UI_LEFT, enabled ? t->color : ui_theme.dim);
}

static void check_toggle(struct ui_widget *w)
{
	struct textw *t = (struct textw *)w;
	t->checked = !t->checked;
	ui_redraw(w);
	fire_change(w);
	fire(w);
}

static bool check_key(struct ui_widget *w, int key)
{
	if (key == ' ' || key == '\n' || key == '\r') {
		check_toggle(w);
		return true;
	}
	return false;
}

static void check_mouse(struct ui_widget *w, enum ui_mouse what, int x, int y, int buttons)
{
	(void)buttons;
	if (what == UI_RELEASE && inside(w, x, y)) {
		check_toggle(w);
	}
}

static const struct ui_class check_class = {
	.name = "checkbox", .measure = check_measure, .paint = check_paint, .key = check_key,
	.mouse = check_mouse, .destroy = textw_destroy, .set_text = textw_set_text,
	.get_text = textw_get_text,
};

struct ui_widget *ui_checkbox(struct ui_widget *parent, const char *text, bool checked, ui_fn fn,
                              void *arg)
{
	struct textw *t = textw_new(parent, &check_class, text, UI_F_FOCUSABLE);
	if (!t) {
		return NULL;
	}
	t->checked = checked;
	t->w.activate = fn;
	t->w.activate_arg = arg;
	return &t->w;
}

bool ui_checked(const struct ui_widget *w)
{
	return w && w->cls == &check_class && ((const struct textw *)w)->checked;
}

void ui_set_checked(struct ui_widget *w, bool checked)
{
	if (w && w->cls == &check_class && ((struct textw *)w)->checked != checked) {
		((struct textw *)w)->checked = checked;
		ui_redraw(w);
	}
}

/* --- entry: one line of text --- */

struct entry {
	struct ui_widget w;
	char *buf; /* max + 1 bytes */
	int len, max, cursor, off; /* off: the first character shown */
};

#define ENTRY_COLS 20

static void entry_destroy(struct ui_widget *w)
{
	free(((struct entry *)w)->buf);
}

static void entry_measure(struct ui_widget *w)
{
	w->want_w = ENTRY_COLS * GFX_CELL_W + 8;
	w->want_h = GFX_CELL_H + 2 * UI_PAD_Y;
}

static int entry_cols(const struct ui_widget *w)
{
	return ui_imax(1, (w->r.w - 8) / GFX_CELL_W);
}

static void entry_scroll_to_caret(struct entry *e)
{
	int cols = entry_cols(&e->w);
	if (e->cursor < e->off) {
		e->off = e->cursor;
	} else if (e->cursor > e->off + cols - 1) {
		e->off = e->cursor - (cols - 1);
	}
}

static void entry_paint(struct ui_widget *w, struct gfx_canvas *c)
{
	struct entry *e = (struct entry *)w;
	bool enabled = ui_is_enabled(w);
	ui_draw_frame(w, c, ui_theme.field);
	entry_scroll_to_caret(e);
	char shown[LINE_MAX_CHARS + 1];
	int n = ui_imin(e->len - e->off, ui_imin(entry_cols(w) + 1, LINE_MAX_CHARS));
	memcpy(shown, e->buf + e->off, (size_t)ui_imax(n, 0));
	shown[ui_imax(n, 0)] = '\0';
	int ty = w->r.y + (w->r.h - GFX_CELL_H) / 2;
	gfx_text(c, w->r.x + 4, ty, shown, enabled ? ui_theme.text : ui_theme.dim, 1);
	if (ui_is_focused(w)) {
		gfx_vline(c, w->r.x + 4 + (e->cursor - e->off) * GFX_CELL_W, ty + 1, 9, ui_theme.accent);
	}
}

static void entry_edit(struct ui_widget *w)
{
	ui_redraw(w);
	fire_change(w);
}

static bool entry_key(struct ui_widget *w, int key)
{
	struct entry *e = (struct entry *)w;
	switch (key) {
	case '\n':
	case '\r':
		fire(w);
		return true;
	case '\b':
	case 0x7F:
		if (e->cursor > 0) {
			memmove(e->buf + e->cursor - 1, e->buf + e->cursor, (size_t)(e->len - e->cursor + 1));
			e->cursor--;
			e->len--;
			entry_edit(w);
		}
		return true;
	case ZKT_KEY_DELETE:
		if (e->cursor < e->len) {
			memmove(e->buf + e->cursor, e->buf + e->cursor + 1, (size_t)(e->len - e->cursor));
			e->len--;
			entry_edit(w);
		}
		return true;
	case ZKT_KEY_LEFT:
		e->cursor = ui_imax(0, e->cursor - 1);
		break;
	case ZKT_KEY_RIGHT:
		e->cursor = ui_imin(e->len, e->cursor + 1);
		break;
	case ZKT_KEY_HOME:
	case 0x01: /* ^A */
		e->cursor = 0;
		break;
	case ZKT_KEY_END:
	case 0x05: /* ^E */
		e->cursor = e->len;
		break;
	case 0x0B: /* ^K: to the end of the line */
		e->len = e->cursor;
		e->buf[e->len] = '\0';
		entry_edit(w);
		return true;
	case 0x15: /* ^U: all of it */
		e->len = e->cursor = e->off = 0;
		e->buf[0] = '\0';
		entry_edit(w);
		return true;
	default:
		if (key >= ' ' && key <= '~') {
			if (e->len < e->max) {
				memmove(e->buf + e->cursor + 1, e->buf + e->cursor, (size_t)(e->len - e->cursor + 1));
				e->buf[e->cursor++] = (char)key;
				e->len++;
				entry_edit(w);
			}
			return true;
		}
		return false;
	}
	ui_redraw(w);
	return true;
}

static void entry_mouse(struct ui_widget *w, enum ui_mouse what, int x, int y, int buttons)
{
	struct entry *e = (struct entry *)w;
	(void)y;
	(void)buttons;
	if (what == UI_PRESS || what == UI_DRAG) {
		e->cursor = ui_iclamp(e->off + (x - 4 + GFX_CELL_W / 2) / GFX_CELL_W, 0, e->len);
		ui_redraw(w);
	}
}

static void entry_set_text(struct ui_widget *w, const char *text)
{
	struct entry *e = (struct entry *)w;
	strlcpy(e->buf, text ? text : "", (size_t)e->max + 1);
	e->len = e->cursor = (int)strlen(e->buf);
	e->off = 0;
	ui_redraw(w);
}

static const char *entry_get_text(const struct ui_widget *w)
{
	return ((const struct entry *)w)->buf;
}

static const struct ui_class entry_class = {
	.name = "entry", .measure = entry_measure, .paint = entry_paint, .key = entry_key,
	.mouse = entry_mouse, .destroy = entry_destroy, .set_text = entry_set_text,
	.get_text = entry_get_text,
};

struct ui_widget *ui_entry(struct ui_widget *parent, const char *text, int max, ui_fn fn,
                           void *arg)
{
	struct entry *e = parent ? ui_new_widget(parent, &entry_class, sizeof(*e), UI_F_FOCUSABLE) : NULL;
	if (!e) {
		return NULL;
	}
	e->max = ui_iclamp(max, 1, 4096);
	e->buf = calloc(1, (size_t)e->max + 1);
	if (!e->buf) {
		ui_destroy(&e->w);
		return NULL;
	}
	e->w.activate = fn;
	e->w.activate_arg = arg;
	entry_set_text(&e->w, text);
	return &e->w;
}

/* --- progress bar --- */

struct progress {
	struct ui_widget w;
	int value, max;
};

static void progress_measure(struct ui_widget *w)
{
	w->want_w = 100;
	w->want_h = 15;
}

static void progress_paint(struct ui_widget *w, struct gfx_canvas *c)
{
	struct progress *p = (struct progress *)w;
	ui_draw_frame(w, c, ui_theme.field);
	int inner_w = w->r.w - 2;
	int filled = p->max > 0 ? (int)((long long)inner_w * ui_iclamp(p->value, 0, p->max) / p->max) : 0;
	gfx_fill(c, (struct gfx_rect){ w->r.x + 1, w->r.y + 1, filled, w->r.h - 2 }, ui_theme.accent);
}

static const struct ui_class progress_class = {
	.name = "progress", .measure = progress_measure, .paint = progress_paint,
};

struct ui_widget *ui_progress(struct ui_widget *parent)
{
	struct progress *p = parent ? ui_new_widget(parent, &progress_class, sizeof(*p), 0) : NULL;
	if (!p) {
		return NULL;
	}
	p->max = 100;
	return &p->w;
}

void ui_set_progress(struct ui_widget *w, int value, int max)
{
	if (w && w->cls == &progress_class) {
		struct progress *p = (struct progress *)w;
		if (p->value != value || p->max != max) {
			p->value = value;
			p->max = max;
			ui_redraw(w);
		}
	}
}

/* --- separator and spacer --- */

static void separator_measure(struct ui_widget *w)
{
	bool across = ui_is_horizontal(w->parent); /* in a row it is a vertical line */
	w->want_w = across ? 3 : 0;
	w->want_h = across ? 0 : 3;
}

static void separator_paint(struct ui_widget *w, struct gfx_canvas *c)
{
	if (ui_is_horizontal(w->parent)) {
		gfx_vline(c, w->r.x + w->r.w / 2, w->r.y, w->r.h, ui_theme.edge);
	} else {
		gfx_hline(c, w->r.x, w->r.y + w->r.h / 2, w->r.w, ui_theme.edge);
	}
}

static const struct ui_class separator_class = {
	.name = "separator", .measure = separator_measure, .paint = separator_paint,
};
static const struct ui_class spacer_class = { .name = "spacer" };

struct ui_widget *ui_separator(struct ui_widget *parent)
{
	struct ui_widget *w = parent ? ui_new_widget(parent, &separator_class, sizeof(*w), 0) : NULL;
	return w;
}

struct ui_widget *ui_spacer(struct ui_widget *parent)
{
	struct ui_widget *w = parent ? ui_new_widget(parent, &spacer_class, sizeof(*w), 0) : NULL;
	if (w) {
		w->expand = 1;
	}
	return w;
}

/* --- custom: the program's own --- */

struct custom {
	struct ui_widget w;
	struct ui_custom ops;
	void *arg;
	int last_w, last_h;
};

static void custom_paint(struct ui_widget *w, struct gfx_canvas *c)
{
	struct custom *x = (struct custom *)w;
	if (x->ops.paint) {
		x->ops.paint(w, c, x->arg);
	}
}

static bool custom_key(struct ui_widget *w, int key)
{
	struct custom *x = (struct custom *)w;
	return x->ops.key && x->ops.key(w, key, x->arg);
}

static void custom_mouse(struct ui_widget *w, enum ui_mouse what, int x, int y, int buttons)
{
	struct custom *c = (struct custom *)w;
	if (c->ops.mouse) {
		c->ops.mouse(w, what, x, y, buttons, c->arg);
	}
}

static void custom_layout(struct ui_widget *w)
{
	struct custom *x = (struct custom *)w;
	if ((x->last_w != w->r.w || x->last_h != w->r.h) && x->ops.resized) {
		x->last_w = w->r.w;
		x->last_h = w->r.h;
		x->ops.resized(w, x->arg);
	}
}

static const struct ui_class custom_class = {
	.name = "custom", .paint = custom_paint, .key = custom_key, .mouse = custom_mouse,
	.layout = custom_layout,
};

struct ui_widget *ui_custom(struct ui_widget *parent, const struct ui_custom *ops, void *arg)
{
	struct custom *x = parent && ops
	    ? ui_new_widget(parent, &custom_class, sizeof(*x), ops->focusable ? UI_F_FOCUSABLE : 0)
	    : NULL;
	if (!x) {
		return NULL;
	}
	x->ops = *ops;
	x->arg = arg;
	return &x->w;
}
