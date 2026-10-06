/* libui's insides, shared by its source files (ui.h is the interface):
 *
 *   ui.c      windows, the event loop, the widget tree, drawing, focus
 *   layout.c  boxes and grids: measuring and placing children
 *   widgets.c label, button, checkbox, entry, progress, separator, custom
 *   list.c    the list
 *   text.c    the multi-line text widget
 *   scroll.c  the scroll bar the list and the text widget share
 */
#ifndef MANIOS_UI_PRIV_H
#define MANIOS_UI_PRIV_H

#include "ui.h"
#include <stddef.h>

#define UI_F_HIDDEN    1u
#define UI_F_DISABLED  2u
#define UI_F_FOCUSABLE 4u

/* A widget's behaviour; any of the functions may be NULL. */
struct ui_class {
	const char *name;
	/* Sets w->want_w and want_h; the children have been measured. */
	void (*measure)(struct ui_widget *w);
	/* Places the children (w->r is final). */
	void (*layout)(struct ui_widget *w);
	/* Draws the widget itself into c (clipped to the area being drawn);
	 * the children are drawn after. */
	void (*paint)(struct ui_widget *w, struct gfx_canvas *c);
	/* A key (not Alt's, not for ManiDE); true if used. */
	bool (*key)(struct ui_widget *w, int key);
	/* The pointer, relative to w->r: pressed in it, then dragged and
	 * released wherever it goes; or moving over it. */
	void (*mouse)(struct ui_widget *w, enum ui_mouse what, int x, int y, int buttons);
	/* Frees what the widget holds (not the widget). */
	void (*destroy)(struct ui_widget *w);
	/* ui_set_text and ui_get_text. */
	void (*set_text)(struct ui_widget *w, const char *text);
	const char *(*get_text)(const struct ui_widget *w);
};

struct ui {
	struct win *win;           /* NULL: no window; drawing on `own` */
	struct gfx_canvas *own;
	int width, height;
	struct ui_widget *root;
	struct ui_widget *focus;   /* gets the keys */
	struct ui_widget *hover;   /* under the pointer */
	struct ui_widget *grab;    /* a press began here: gets the pointer until release */
	struct ui_widget *modal;   /* the message, over everything */
	struct ui_widget *before_modal; /* what had the focus under it */
	bool active;               /* the window has the focus (the desktop's) */
	bool need_layout;
	bool quit;
	struct gfx_rect dirty;     /* to draw; empty: nothing */
	int buttons;               /* the pointer's buttons at the last report */
	ui_key_fn key_fn;
	void *key_arg;
	ui_close_fn close_fn;
	void *close_arg;
	int timer_ms;
	uint32_t timer_next;
	ui_tick_fn timer_fn;
	void *timer_arg;
};

/* A new widget of `size` bytes (its first member a struct ui_widget),
 * zeroed, added at the end of parent. NULL if out of memory. */
void *ui_new_widget(struct ui_widget *parent, const struct ui_class *cls, size_t size,
                    unsigned flags);
/* Changed in a way that changes sizes: measure and place everything again. */
void ui_relayout(struct ui *ui);
/* Copies s into *dst (freeing what was there); false if out of memory. */
bool ui_copy_string(char **dst, const char *s);

static inline bool ui_is_enabled(const struct ui_widget *w) { return !(w->flags & UI_F_DISABLED); }
static inline int ui_imax(int a, int b) { return a > b ? a : b; }
static inline int ui_imin(int a, int b) { return a < b ? a : b; }
static inline int ui_iclamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The widgets' shared look. */
#define UI_PAD_X 8
#define UI_PAD_Y 4
#define UI_ROW_H (GFX_CELL_H + 2)
#define UI_BAR_W 9 /* a scroll bar's width */

/* Draws w's border: the focus colour when it has the focus. */
void ui_draw_frame(struct ui_widget *w, struct gfx_canvas *c, gfx_color fill);
bool ui_is_focused(const struct ui_widget *w); /* has the focus, and the window does too */

/* layout.c: a box or grid of class cls (ui_vbox_class, ui_hbox_class,
 * ui_grid_class) in parent -- or, with no parent, a free-standing one of
 * `ui`'s (the root, the message). */
extern const struct ui_class ui_vbox_class, ui_hbox_class, ui_grid_class;
struct ui_widget *ui_box_new(struct ui *ui, struct ui_widget *parent, const struct ui_class *cls,
                             int spacing, int columns);
bool ui_is_horizontal(const struct ui_widget *box);

/* scroll.c: a vertical scroll bar at the right of `area`, for `total`
 * lines of which `visible` show from line `top`. */
struct ui_scroll {
	int dragging;  /* the pointer's offset in the thumb during a drag, else -1 */
};
struct gfx_rect ui_scroll_bar(struct gfx_rect area);
void ui_scroll_paint(struct gfx_canvas *c, struct gfx_rect area, int total, int visible, int top);
/* A press (0) or drag (1) at y within the bar: the new top line, or -1
 * if the press was not on the bar. A click on the track pages. */
int ui_scroll_mouse(struct ui_scroll *s, struct gfx_rect area, int total, int visible, int top,
                    enum ui_mouse what, int x, int y);

#endif
