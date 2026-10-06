/* The multi-line text widget (ui.h): text with a caret and a scroll bar,
 * editable or not. The text is one buffer; the start of every line is
 * found again whenever it changes. Columns are display columns: a Tab
 * character stands for spaces up to a multiple of TAB_COLS, and a
 * carriage return takes no room (so DOS's line ends look right). */
#include "ui_priv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zkt_abi.h>

#define TAB_COLS 4
#define NUMBER_DIGITS 5
#define MARGIN 3  /* left of the text, inside the border */
#define SHOWN_MAX 500

struct text {
	struct ui_widget w;
	char *buf; /* NUL-terminated */
	size_t len, cap;
	size_t cursor;
	size_t *starts; /* where each line starts */
	int nlines, line_cap;
	bool stale;     /* starts must be found again */
	int top, left;  /* the first line and display column shown */
	int want_col;   /* where Up and Down aim */
	bool readonly, numbers;
	struct ui_scroll scroll;
};

static void text_measure(struct ui_widget *w)
{
	w->want_w = 160;
	w->want_h = 4 * GFX_CELL_H + 4;
}

/* Finds the line starts. False if out of memory. */
static bool index_lines(struct text *t)
{
	if (!t->stale) {
		return true;
	}
	int n = 1;
	for (size_t i = 0; i < t->len; i++) {
		n += t->buf[i] == '\n';
	}
	if (n > t->line_cap) {
		size_t *starts = realloc(t->starts, (size_t)n * sizeof(*starts));
		if (!starts) {
			return false;
		}
		t->starts = starts;
		t->line_cap = n;
	}
	int line = 1;
	t->starts[0] = 0;
	for (size_t i = 0; i < t->len; i++) {
		if (t->buf[i] == '\n') {
			t->starts[line++] = i + 1;
		}
	}
	t->nlines = n;
	t->stale = false;
	return true;
}

/* The lines there is something to show of: a read-only text that ends
 * with a newline has no empty line after it. */
static int shown_lines(const struct text *t)
{
	return t->readonly && t->nlines > 1 && t->starts[t->nlines - 1] == t->len ? t->nlines - 1
	                                                                           : t->nlines;
}

static size_t line_end(const struct text *t, int line)
{
	return line + 1 < t->nlines ? t->starts[line + 1] - 1 : t->len;
}

static int line_of(const struct text *t, size_t offset)
{
	int lo = 0, hi = t->nlines - 1;
	while (lo < hi) {
		int mid = (lo + hi + 1) / 2;
		if (t->starts[mid] <= offset) {
			lo = mid;
		} else {
			hi = mid - 1;
		}
	}
	return lo;
}

static int advance(int col, char ch)
{
	if (ch == '\t') {
		return (col / TAB_COLS + 1) * TAB_COLS;
	}
	return ch == '\r' ? col : col + 1;
}

static int col_of(const struct text *t, int line, size_t offset)
{
	int col = 0;
	for (size_t i = t->starts[line]; i < offset && i < t->len; i++) {
		col = advance(col, t->buf[i]);
	}
	return col;
}

/* The offset in `line` that is at display column col, or its end. */
static size_t offset_for(const struct text *t, int line, int col)
{
	size_t end = line_end(t, line), i = t->starts[line];
	for (int at = 0; i < end && at < col;) {
		at = advance(at, t->buf[i++]);
	}
	return i;
}

/* --- where things are --- */

static int gutter(const struct text *t)
{
	return t->numbers ? NUMBER_DIGITS * GFX_CELL_W + 4 : 0;
}

static int rows_shown(const struct ui_widget *w)
{
	return ui_imax(1, (w->r.h - 4) / GFX_CELL_H);
}

static int cols_shown(const struct text *t)
{
	return ui_imax(1, (t->w.r.w - 2 - MARGIN - gutter(t) - UI_BAR_W) / GFX_CELL_W);
}

static void clamp_view(struct text *t)
{
	t->top = ui_iclamp(t->top, 0, ui_imax(0, shown_lines(t) - rows_shown(&t->w)));
	t->left = ui_imax(0, t->left);
}

/* Scrolls the least that shows the caret. */
static void show_caret(struct text *t)
{
	if (!index_lines(t)) {
		return;
	}
	int line = line_of(t, t->cursor), col = col_of(t, line, t->cursor);
	int rows = rows_shown(&t->w), cols = cols_shown(t);
	if (line < t->top) {
		t->top = line;
	} else if (line >= t->top + rows) {
		t->top = line - rows + 1;
	}
	if (col < t->left) {
		t->left = col;
	} else if (col >= t->left + cols) {
		t->left = col - cols + 1;
	}
	clamp_view(t);
}

/* --- painting --- */

static void text_paint(struct ui_widget *w, struct gfx_canvas *c)
{
	struct text *t = (struct text *)w;
	ui_draw_frame(w, c, ui_theme.field);
	if (!index_lines(t)) {
		return;
	}
	clamp_view(t);
	int rows = rows_shown(w), cols = ui_imin(cols_shown(t), SHOWN_MAX);
	int x0 = w->r.x + 1 + MARGIN + gutter(t), y0 = w->r.y + 2, total = shown_lines(t);
	char shown[SHOWN_MAX + 2];
	for (int r = 0; r < rows && t->top + r < total; r++) {
		int line = t->top + r, y = y0 + r * GFX_CELL_H;
		if (t->numbers) {
			char num[NUMBER_DIGITS + 2];
			snprintf(num, sizeof(num), "%*d", NUMBER_DIGITS, line + 1);
			gfx_text(c, w->r.x + 1 + MARGIN, y, num, ui_theme.dim, 1);
		}
		int col = 0, n = 0;
		memset(shown, ' ', (size_t)cols + 1);
		for (size_t i = t->starts[line], end = line_end(t, line); i < end && col < t->left + cols + 1;
		     i++) {
			char ch = t->buf[i];
			int next = advance(col, ch);
			if (ch != '\t' && ch != '\r' && col >= t->left) {
				shown[col - t->left] = ch >= ' ' && ch <= '~' ? ch : '.';
				n = col - t->left + 1;
			}
			col = next;
		}
		shown[n] = '\0';
		gfx_text(c, x0, y, shown, ui_theme.text, 1);
	}
	if (!t->readonly && ui_is_focused(w)) {
		int line = line_of(t, t->cursor), col = col_of(t, line, t->cursor);
		if (line >= t->top && line < t->top + rows && col >= t->left && col <= t->left + cols) {
			gfx_vline(c, x0 + (col - t->left) * GFX_CELL_W, y0 + (line - t->top) * GFX_CELL_H + 1, 9,
			          ui_theme.accent);
		}
	}
	ui_scroll_paint(c, (struct gfx_rect){ w->r.x + 1, w->r.y + 1, w->r.w - 2, w->r.h - 2 }, total,
	                rows, t->top);
}

/* --- editing --- */

static void changed(struct text *t)
{
	t->stale = true;
	index_lines(t);
	show_caret(t);
	ui_redraw(&t->w);
	if (t->w.change) {
		t->w.change(&t->w, t->w.change_arg);
	}
}

static bool grow(struct text *t, size_t extra)
{
	if (t->len + extra + 1 <= t->cap) {
		return true;
	}
	size_t cap = t->cap ? t->cap : 256;
	while (cap < t->len + extra + 1) {
		cap *= 2;
	}
	char *buf = realloc(t->buf, cap);
	if (!buf) {
		return false;
	}
	t->buf = buf;
	t->cap = cap;
	return true;
}

static void insert(struct text *t, const char *s, size_t n)
{
	if (!grow(t, n)) {
		return;
	}
	memmove(t->buf + t->cursor + n, t->buf + t->cursor, t->len - t->cursor + 1);
	memcpy(t->buf + t->cursor, s, n);
	t->cursor += n;
	t->len += n;
	changed(t);
	t->want_col = col_of(t, line_of(t, t->cursor), t->cursor);
}

static void erase(struct text *t, size_t from, size_t to)
{
	if (from >= to || to > t->len) {
		return;
	}
	memmove(t->buf + from, t->buf + to, t->len - to + 1);
	t->len -= to - from;
	t->cursor = from;
	changed(t);
	t->want_col = col_of(t, line_of(t, t->cursor), t->cursor);
}

static void move_to_line(struct text *t, int line)
{
	line = ui_iclamp(line, 0, t->nlines - 1);
	t->cursor = offset_for(t, line, t->want_col);
	show_caret(t);
	ui_redraw(&t->w);
}

static void move_to(struct text *t, size_t offset)
{
	t->cursor = offset;
	t->want_col = col_of(t, line_of(t, offset), offset);
	show_caret(t);
	ui_redraw(&t->w);
}

static bool text_key(struct ui_widget *w, int key)
{
	struct text *t = (struct text *)w;
	if (!index_lines(t)) {
		return false;
	}
	int line = line_of(t, t->cursor), rows = rows_shown(w);
	if (t->readonly) {
		int last = ui_imax(0, shown_lines(t) - rows), top = t->top, left = t->left;
		switch (key) {
		case ZKT_KEY_UP:
			top--;
			break;
		case ZKT_KEY_DOWN:
			top++;
			break;
		case ZKT_KEY_PGUP:
			top -= rows - 1;
			break;
		case ZKT_KEY_PGDN:
			top += rows - 1;
			break;
		case ZKT_KEY_HOME:
			top = 0;
			left = 0;
			break;
		case ZKT_KEY_END:
			top = last;
			break;
		case ZKT_KEY_LEFT:
			left -= TAB_COLS;
			break;
		case ZKT_KEY_RIGHT:
			left += TAB_COLS;
			break;
		default:
			return false;
		}
		t->top = ui_iclamp(top, 0, last);
		t->left = ui_imax(0, left);
		ui_redraw(w);
		return true;
	}
	switch (key) {
	case ZKT_KEY_UP:
		move_to_line(t, line - 1);
		return true;
	case ZKT_KEY_DOWN:
		move_to_line(t, line + 1);
		return true;
	case ZKT_KEY_PGUP:
		move_to_line(t, line - rows + 1);
		return true;
	case ZKT_KEY_PGDN:
		move_to_line(t, line + rows - 1);
		return true;
	case ZKT_KEY_LEFT:
		move_to(t, t->cursor ? t->cursor - 1 : 0);
		return true;
	case ZKT_KEY_RIGHT:
		move_to(t, t->cursor < t->len ? t->cursor + 1 : t->len);
		return true;
	case ZKT_KEY_HOME:
	case 0x01: /* ^A */
		move_to(t, t->starts[line]);
		return true;
	case ZKT_KEY_END:
	case 0x05: /* ^E */
		move_to(t, line_end(t, line));
		return true;
	case '\b':
	case 0x7F:
		erase(t, t->cursor ? t->cursor - 1 : 0, t->cursor);
		return true;
	case ZKT_KEY_DELETE:
		erase(t, t->cursor, t->cursor + 1);
		return true;
	case 0x0B: /* ^K: to the end of the line, or its newline */
		erase(t, t->cursor, t->cursor == line_end(t, line) ? t->cursor + 1 : line_end(t, line));
		return true;
	case '\n':
	case '\r':
		insert(t, "\n", 1);
		return true;
	default:
		if (key >= ' ' && key <= '~') {
			char ch = (char)key;
			insert(t, &ch, 1);
			return true;
		}
		return false;
	}
}

static void text_mouse(struct ui_widget *w, enum ui_mouse what, int x, int y, int buttons)
{
	struct text *t = (struct text *)w;
	(void)buttons;
	if (!index_lines(t)) {
		return;
	}
	int rows = rows_shown(w), total = shown_lines(t);
	int top = ui_scroll_mouse(&t->scroll, (struct gfx_rect){ 1, 1, w->r.w - 2, w->r.h - 2 }, total,
	                          rows, t->top, what, x, y);
	if (top >= 0) {
		if (top != t->top) {
			t->top = top;
			ui_redraw(w);
		}
		return;
	}
	if (what != UI_PRESS && what != UI_DRAG) {
		return;
	}
	int line = ui_iclamp(t->top + (y - 2) / GFX_CELL_H, 0, ui_imax(0, total - 1));
	int col = ui_imax(0, t->left + (x - 1 - MARGIN - gutter(t) + GFX_CELL_W / 2) / GFX_CELL_W);
	t->cursor = offset_for(t, line, col);
	t->want_col = col_of(t, line, t->cursor);
	ui_redraw(w);
}

static void text_set_text(struct ui_widget *w, const char *text)
{
	struct text *t = (struct text *)w;
	size_t n = text ? strlen(text) : 0;
	if (!grow(t, n)) {
		return; /* out of memory: the text stays as it was */
	}
	memcpy(t->buf, text ? text : "", n + 1);
	t->len = n;
	t->cursor = 0;
	t->top = t->left = t->want_col = 0;
	t->stale = true;
	index_lines(t);
	ui_redraw(w);
}

static const char *text_get_text(const struct ui_widget *w)
{
	return ((const struct text *)w)->buf;
}

static void text_destroy(struct ui_widget *w)
{
	struct text *t = (struct text *)w;
	free(t->buf);
	free(t->starts);
}

static const struct ui_class text_class = {
	.name = "text", .measure = text_measure, .paint = text_paint, .key = text_key,
	.mouse = text_mouse, .destroy = text_destroy, .set_text = text_set_text,
	.get_text = text_get_text,
};

struct ui_widget *ui_text(struct ui_widget *parent, bool readonly)
{
	struct text *t = parent ? ui_new_widget(parent, &text_class, sizeof(*t), UI_F_FOCUSABLE) : NULL;
	if (!t) {
		return NULL;
	}
	t->readonly = readonly;
	t->scroll.dragging = -1;
	if (!grow(t, 0)) {
		ui_destroy(&t->w);
		return NULL;
	}
	t->buf[0] = '\0';
	t->stale = true;
	index_lines(t);
	return &t->w;
}

static struct text *as_text(const struct ui_widget *w)
{
	return w && w->cls == &text_class ? (struct text *)w : NULL;
}

void ui_text_numbers(struct ui_widget *w, bool on)
{
	struct text *t = as_text(w);
	if (t && t->numbers != on) {
		t->numbers = on;
		ui_redraw(w);
	}
}

int ui_text_lines(const struct ui_widget *w)
{
	struct text *t = as_text(w);
	return t && index_lines(t) ? shown_lines(t) : 0;
}

int ui_text_line(const struct ui_widget *w)
{
	struct text *t = as_text(w);
	return t && index_lines(t) ? line_of(t, t->cursor) : 0;
}

int ui_text_column(const struct ui_widget *w)
{
	struct text *t = as_text(w);
	return t && index_lines(t) ? col_of(t, line_of(t, t->cursor), t->cursor) : 0;
}

int ui_text_top(const struct ui_widget *w)
{
	struct text *t = as_text(w);
	return t ? t->top : 0;
}

void ui_text_goto(struct ui_widget *w, int line)
{
	struct text *t = as_text(w);
	if (t && index_lines(t)) {
		line = ui_iclamp(line, 0, shown_lines(t) - 1);
		t->cursor = t->starts[line];
		t->want_col = 0;
		t->top = line;
		clamp_view(t);
		ui_redraw(w);
	}
}
