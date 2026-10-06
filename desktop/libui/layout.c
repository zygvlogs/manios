/* Boxes and grids (ui.h): measuring them and placing their children. */
#include "ui_priv.h"
#include <stdlib.h>

struct box {
	struct ui_widget w;
	int spacing, pad, columns;
};

static int visible_count(const struct ui_widget *w)
{
	int n = 0;
	for (const struct ui_widget *k = w->first; k; k = k->next) {
		n += !(k->flags & UI_F_HIDDEN);
	}
	return n;
}

static void line_measure(struct ui_widget *w)
{
	struct box *b = (struct box *)w;
	bool horizontal = w->cls == &ui_hbox_class;
	int n = 0, along = 0, across = 0;
	for (struct ui_widget *k = w->first; k; k = k->next) {
		if (k->flags & UI_F_HIDDEN) {
			continue;
		}
		n++;
		along += horizontal ? k->want_w : k->want_h;
		across = ui_imax(across, horizontal ? k->want_h : k->want_w);
	}
	along += b->spacing * (n ? n - 1 : 0);
	w->want_w = (horizontal ? along : across) + 2 * b->pad;
	w->want_h = (horizontal ? across : along) + 2 * b->pad;
}

/* Gives each child its size along the box, then its place. The room left
 * over goes to the children with `expand`, in proportion; a box too small
 * takes room back from those first, then from the others, last to first. */
static void line_layout(struct ui_widget *w)
{
	struct box *b = (struct box *)w;
	bool horizontal = w->cls == &ui_hbox_class;
	struct gfx_rect in = { w->r.x + b->pad, w->r.y + b->pad, w->r.w - 2 * b->pad,
		               w->r.h - 2 * b->pad };
	int n = visible_count(w), wanted = 0, weights = 0;
	for (struct ui_widget *k = w->first; k; k = k->next) {
		if (!(k->flags & UI_F_HIDDEN)) {
			wanted += horizontal ? k->want_w : k->want_h;
			weights += k->expand;
		}
	}
	int room = (horizontal ? in.w : in.h) - b->spacing * (n ? n - 1 : 0);
	int extra = room - wanted;

	/* The sizes along the box go in the children's rectangles for now. */
	int left = extra > 0 ? extra : 0, weights_left = weights;
	for (struct ui_widget *k = w->first; k; k = k->next) {
		if (k->flags & UI_F_HIDDEN) {
			continue;
		}
		int size = horizontal ? k->want_w : k->want_h;
		if (extra > 0 && k->expand) {
			int share = left * k->expand / weights_left;
			size += share;
			left -= share;
			weights_left -= k->expand;
		}
		if (horizontal) {
			k->r.w = size;
		} else {
			k->r.h = size;
		}
	}
	for (int pass = 0; extra < 0 && pass < 2; pass++) {
		/* Last to first: expanding children, then the rest. */
		for (struct ui_widget *k = w->last; extra < 0 && k;) {
			if (!(k->flags & UI_F_HIDDEN) && (k->expand != 0) == (pass == 0)) {
				int *size = horizontal ? &k->r.w : &k->r.h;
				int take = ui_imin(-extra, *size);
				*size -= take;
				extra += take;
			}
			/* (singly linked: find the one before k) */
			struct ui_widget *prev = NULL;
			for (struct ui_widget *s = w->first; s != k; s = s->next) {
				prev = s;
			}
			k = prev;
		}
	}
	int at = horizontal ? in.x : in.y;
	for (struct ui_widget *k = w->first; k; k = k->next) {
		if (k->flags & UI_F_HIDDEN) {
			continue;
		}
		int size = horizontal ? k->r.w : k->r.h;
		k->r = horizontal ? (struct gfx_rect){ at, in.y, size, in.h }
		                  : (struct gfx_rect){ in.x, at, in.w, size };
		at += size + b->spacing;
	}
}

static void grid_measure(struct ui_widget *w)
{
	struct box *b = (struct box *)w;
	int cols = ui_imax(1, b->columns), n = 0, cell_w = 0, cell_h = 0;
	for (struct ui_widget *k = w->first; k; k = k->next) {
		if (!(k->flags & UI_F_HIDDEN)) {
			n++;
			cell_w = ui_imax(cell_w, k->want_w);
			cell_h = ui_imax(cell_h, k->want_h);
		}
	}
	int rows = (n + cols - 1) / cols;
	w->want_w = cols * cell_w + (cols - 1) * b->spacing + 2 * b->pad;
	w->want_h = rows * cell_h + (rows > 0 ? rows - 1 : 0) * b->spacing + 2 * b->pad;
}

/* Cell i of n along a length, with s between them: the remainder is
 * shared out, so the cells differ by at most a pixel. */
static void cell(int start, int length, int n, int s, int i, int *at, int *size)
{
	int room = length - (n - 1) * s;
	room = room < 0 ? 0 : room;
	*at = start + room * i / n + i * s;
	*size = room * (i + 1) / n - room * i / n;
}

static void grid_layout(struct ui_widget *w)
{
	struct box *b = (struct box *)w;
	int cols = ui_imax(1, b->columns), n = visible_count(w), rows = (n + cols - 1) / cols, i = 0;
	struct gfx_rect in = { w->r.x + b->pad, w->r.y + b->pad, w->r.w - 2 * b->pad,
		               w->r.h - 2 * b->pad };
	for (struct ui_widget *k = w->first; k; k = k->next) {
		if (k->flags & UI_F_HIDDEN) {
			continue;
		}
		cell(in.x, in.w, cols, b->spacing, i % cols, &k->r.x, &k->r.w);
		cell(in.y, in.h, rows, b->spacing, i / cols, &k->r.y, &k->r.h);
		i++;
	}
}

const struct ui_class ui_vbox_class = { .name = "vbox", .measure = line_measure, .layout = line_layout };
const struct ui_class ui_hbox_class = { .name = "hbox", .measure = line_measure, .layout = line_layout };
const struct ui_class ui_grid_class = { .name = "grid", .measure = grid_measure, .layout = grid_layout };

bool ui_is_horizontal(const struct ui_widget *box)
{
	return box && box->cls == &ui_hbox_class;
}

struct ui_widget *ui_box_new(struct ui *ui, struct ui_widget *parent, const struct ui_class *cls,
                             int spacing, int columns)
{
	struct box *b;
	if (parent) {
		b = ui_new_widget(parent, cls, sizeof(*b), 0);
	} else {
		b = calloc(1, sizeof(*b));
		if (b) {
			b->w.cls = cls;
			b->w.ui = ui;
		}
	}
	if (!b) {
		return NULL;
	}
	b->spacing = spacing;
	b->columns = columns;
	return &b->w;
}

struct ui_widget *ui_vbox(struct ui_widget *parent, int spacing)
{
	return parent ? ui_box_new(parent->ui, parent, &ui_vbox_class, spacing, 0) : NULL;
}

struct ui_widget *ui_hbox(struct ui_widget *parent, int spacing)
{
	return parent ? ui_box_new(parent->ui, parent, &ui_hbox_class, spacing, 0) : NULL;
}

struct ui_widget *ui_grid(struct ui_widget *parent, int columns, int spacing)
{
	return parent ? ui_box_new(parent->ui, parent, &ui_grid_class, spacing, columns) : NULL;
}

void ui_set_padding(struct ui_widget *box, int pad)
{
	if (box && (box->cls == &ui_vbox_class || box->cls == &ui_hbox_class
	            || box->cls == &ui_grid_class)) {
		((struct box *)box)->pad = pad < 0 ? 0 : pad;
		ui_relayout(box->ui);
	}
}
