/* The list widget (ui.h): rows of text, one selected, with a scroll bar. */
#include "ui_priv.h"
#include <manios.h>
#include <stdlib.h>
#include <string.h>
#include <zkt_abi.h>

#define DOUBLE_CLICK_MS 500
#define ROW_PAD 6
#define LINE_MAX_CHARS 255

struct item {
	char *text;
	gfx_color color;
	void *data;
};

struct list {
	struct ui_widget w;
	struct item *items;
	int count, cap;
	int selected; /* -1: none */
	int top;      /* the first row shown */
	struct ui_scroll scroll;
	uint32_t clicked_at; /* a double click: the same row, soon */
	int clicked_row;
};

static struct gfx_rect inner(const struct ui_widget *w)
{
	return (struct gfx_rect){ w->r.x + 1, w->r.y + 1, w->r.w - 2, w->r.h - 2 };
}

static int visible_rows(const struct ui_widget *w)
{
	return ui_imax(1, (w->r.h - 2) / UI_ROW_H);
}

static void list_measure(struct ui_widget *w)
{
	w->want_w = 120;
	w->want_h = 3 * UI_ROW_H + 2;
}

static void clamp_top(struct list *l)
{
	l->top = ui_iclamp(l->top, 0, ui_imax(0, l->count - visible_rows(&l->w)));
}

/* Scrolls the least that shows the selected row. */
static void show_selected(struct list *l)
{
	int rows = visible_rows(&l->w);
	if (l->selected < l->top) {
		l->top = l->selected;
	} else if (l->selected >= l->top + rows) {
		l->top = l->selected - rows + 1;
	}
	clamp_top(l);
}

static void list_paint(struct ui_widget *w, struct gfx_canvas *c)
{
	struct list *l = (struct list *)w;
	ui_draw_frame(w, c, ui_theme.field);
	clamp_top(l);
	struct gfx_rect in = inner(w);
	int rows = visible_rows(w), text_right = in.x + in.w - UI_BAR_W - ROW_PAD;
	for (int i = 0; i < rows && l->top + i < l->count; i++) {
		const struct item *it = &l->items[l->top + i];
		struct gfx_rect row = { in.x, in.y + i * UI_ROW_H, in.w - UI_BAR_W, UI_ROW_H };
		bool selected = l->top + i == l->selected;
		if (selected) {
			gfx_fill(c, row, ui_theme.select);
			if (ui_is_focused(w)) {
				gfx_outline(c, row, ui_theme.focus);
			}
		}
		int ty = row.y + (UI_ROW_H - GFX_CELL_H) / 2;
		const char *tab = strchr(it->text, '\t');
		char name[LINE_MAX_CHARS + 1];
		size_t n = tab ? (size_t)(tab - it->text) : strlen(it->text);
		n = n < LINE_MAX_CHARS ? n : LINE_MAX_CHARS;
		memcpy(name, it->text, n);
		name[n] = '\0';
		gfx_text(c, row.x + ROW_PAD, ty, name, it->color ? it->color : ui_theme.text, 1);
		if (tab) {
			gfx_text(c, text_right - gfx_text_width(tab + 1, 1), ty, tab + 1, ui_theme.dim, 1);
		}
	}
	ui_scroll_paint(c, in, l->count, rows, l->top);
}

static void select_row(struct list *l, int row, bool user)
{
	row = ui_iclamp(row, l->count ? 0 : -1, l->count - 1);
	if (row == l->selected) {
		show_selected(l);
		ui_redraw(&l->w);
		return;
	}
	l->selected = row;
	if (row >= 0) {
		show_selected(l);
	}
	ui_redraw(&l->w);
	if (user && l->w.change) {
		l->w.change(&l->w, l->w.change_arg);
	}
}

static bool list_key(struct ui_widget *w, int key)
{
	struct list *l = (struct list *)w;
	int rows = visible_rows(w), now = l->selected < 0 ? 0 : l->selected;
	switch (key) {
	case ZKT_KEY_UP:
		select_row(l, l->selected < 0 ? l->count - 1 : now - 1, true);
		return true;
	case ZKT_KEY_DOWN:
		select_row(l, l->selected < 0 ? 0 : now + 1, true);
		return true;
	case ZKT_KEY_PGUP:
		select_row(l, now - rows, true);
		return true;
	case ZKT_KEY_PGDN:
		select_row(l, now + rows, true);
		return true;
	case ZKT_KEY_HOME:
		select_row(l, 0, true);
		return true;
	case ZKT_KEY_END:
		select_row(l, l->count - 1, true);
		return true;
	case '\n':
	case '\r':
		if (l->selected >= 0 && w->activate) {
			w->activate(w, w->activate_arg);
		}
		return true;
	}
	return false;
}

static void list_mouse(struct ui_widget *w, enum ui_mouse what, int x, int y, int buttons)
{
	struct list *l = (struct list *)w;
	(void)buttons;
	struct gfx_rect in = { 1, 1, w->r.w - 2, w->r.h - 2 }; /* relative to the widget */
	int total = l->count, rows = visible_rows(w);
	int top = ui_scroll_mouse(&l->scroll, in, total, rows, l->top, what, x, y);
	if (top >= 0) {
		if (top != l->top) {
			l->top = top;
			ui_redraw(w);
		}
		return;
	}
	if (what != UI_PRESS || x >= in.x + in.w - UI_BAR_W) {
		return;
	}
	int row = l->top + (y - 1) / UI_ROW_H;
	if (row < 0 || row >= l->count) {
		return;
	}
	uint32_t now = uptime_ms();
	bool twice = row == l->clicked_row && ((now - l->clicked_at) & 0x7FFFFFFFu) < DOUBLE_CLICK_MS;
	l->clicked_row = row;
	l->clicked_at = now;
	select_row(l, row, true);
	if (twice && w->activate) {
		l->clicked_row = -1;
		w->activate(w, w->activate_arg);
	}
}

static void list_free_items(struct list *l)
{
	for (int i = 0; i < l->count; i++) {
		free(l->items[i].text);
	}
	l->count = 0;
}

static void list_destroy(struct ui_widget *w)
{
	struct list *l = (struct list *)w;
	list_free_items(l);
	free(l->items);
}

static const struct ui_class list_class = {
	.name = "list", .measure = list_measure, .paint = list_paint, .key = list_key,
	.mouse = list_mouse, .destroy = list_destroy,
};

struct ui_widget *ui_list(struct ui_widget *parent, ui_fn fn, void *arg)
{
	struct list *l = parent ? ui_new_widget(parent, &list_class, sizeof(*l), UI_F_FOCUSABLE) : NULL;
	if (!l) {
		return NULL;
	}
	l->selected = -1;
	l->clicked_row = -1;
	l->scroll.dragging = -1;
	l->w.activate = fn;
	l->w.activate_arg = arg;
	return &l->w;
}

static struct list *as_list(const struct ui_widget *w)
{
	return w && w->cls == &list_class ? (struct list *)w : NULL;
}

int ui_list_add(struct ui_widget *w, const char *text, gfx_color color, void *data)
{
	struct list *l = as_list(w);
	if (!l) {
		return -1;
	}
	if (l->count == l->cap) {
		int cap = l->cap ? l->cap * 2 : 32;
		struct item *items = realloc(l->items, (size_t)cap * sizeof(*items));
		if (!items) {
			return -1;
		}
		l->items = items;
		l->cap = cap;
	}
	char *copy = strdup(text ? text : "");
	if (!copy) {
		return -1;
	}
	l->items[l->count] = (struct item){ copy, color, data };
	ui_redraw(w);
	return l->count++;
}

void ui_list_clear(struct ui_widget *w)
{
	struct list *l = as_list(w);
	if (l) {
		list_free_items(l);
		l->selected = -1;
		l->top = 0;
		ui_redraw(w);
	}
}

int ui_list_count(const struct ui_widget *w)
{
	return as_list(w) ? as_list(w)->count : 0;
}

int ui_list_selected(const struct ui_widget *w)
{
	return as_list(w) ? as_list(w)->selected : -1;
}

void ui_list_select(struct ui_widget *w, int index)
{
	struct list *l = as_list(w);
	if (l) {
		select_row(l, index, false);
	}
}

const char *ui_list_text(const struct ui_widget *w, int index)
{
	struct list *l = as_list(w);
	return l && index >= 0 && index < l->count ? l->items[index].text : NULL;
}

void *ui_list_data(const struct ui_widget *w, int index)
{
	struct list *l = as_list(w);
	return l && index >= 0 && index < l->count ? l->items[index].data : NULL;
}
