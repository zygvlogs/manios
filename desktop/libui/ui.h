/* libui: the ManiOS widget toolkit (M23, docs/gui.md). Graphical programs
 * are built from widgets -- labels, buttons, text fields, lists -- placed
 * by boxes and grids; libui lays them out in whatever size ManiDE gives
 * the window, draws them with libgfx in ManiDE's look, and turns keys and
 * the mouse into calls to the program's functions:
 *
 *   struct ui *ui = ui_open("Hello", 240, 120);
 *   ui_label(ui_root(ui), "Hello, ManiOS");
 *   ui_button(ui_root(ui), "Quit", on_quit, ui);
 *   ui_run(ui);                   // until the window is closed
 *   ui_free(ui);
 *
 * libui sits on libwin (windows) and libgfx (drawing); a program may
 * draw and read events itself, through `ui_canvas_of` and `ui_handle`,
 * where the widgets don't do what it needs. Integer arithmetic only.
 *
 * A widget lives until `ui_destroy` or `ui_free`; the toolkit owns the
 * strings it is given a copy of. Nothing here is thread-safe: one ui, one
 * process, one event loop. */
#ifndef MANIOS_UI_H
#define MANIOS_UI_H

#include <gfx.h>
#include <stdbool.h>
#include <win.h>

struct ui;
struct ui_widget;
struct ui_class; /* private (ui_priv.h) */
typedef void (*ui_fn)(struct ui_widget *w, void *arg);

/* The palette: ManiDE's (docs/desktop/DESIGN.md §6). */
struct ui_theme {
	gfx_color bg;        /* a window's background */
	gfx_color panel;     /* buttons, raised things */
	gfx_color panel_hot; /* ... under the pointer */
	gfx_color field;     /* text fields, lists */
	gfx_color edge;      /* borders */
	gfx_color text, dim; /* text; secondary and disabled text */
	gfx_color focus;     /* the focused widget's border (cyan) */
	gfx_color accent;    /* what is current: pressed, filled, the caret (amber) */
	gfx_color select;    /* a selected row's background */
};
extern const struct ui_theme ui_theme;

/* One widget. The fields are for reading; change them through the calls. */
struct ui_widget {
	const struct ui_class *cls;
	struct ui *ui;
	struct ui_widget *parent, *first, *last, *next;
	struct gfx_rect r; /* where it is, in window pixels, after layout */
	int want_w, want_h; /* the size it asks for */
	int min_w, min_h;   /* ui_set_size: it asks for at least this */
	int expand;         /* its share of spare room along its parent's axis */
	unsigned flags;
	gfx_color bg, border; /* ui_set_colors: 0 for none */
	ui_fn activate;       /* what the widget was made with (a button's click, ...) */
	void *activate_arg;
	ui_fn change;         /* ui_on_change */
	void *change_arg;
	void *user;           /* the program's own */
};

typedef void (*ui_tick_fn)(struct ui *ui, void *arg);
/* Called first for every key not meant for ManiDE; true: it is used up. */
typedef bool (*ui_key_fn)(struct ui *ui, int key, void *arg);
/* The window was asked to close; true lets it. */
typedef bool (*ui_close_fn)(struct ui *ui, void *arg);

/* --- a window of widgets --- */

/* A window of this content size (ManiDE chooses the real one); NULL with
 * errno set, ENOENT when no desktop is running. Its root is a vertical box. */
struct ui *ui_open(const char *title, int width, int height);
/* A ui with no window, drawing on a canvas of its own: for tests and
 * for rendering. Feed it events with ui_handle. */
struct ui *ui_new(int width, int height);
void ui_free(struct ui *ui); /* closes the window; frees every widget */
struct ui_widget *ui_root(struct ui *ui);
struct gfx_canvas *ui_canvas_of(struct ui *ui); /* what the widgets are drawn on */
void ui_set_title(struct ui *ui, const char *title);
int ui_width(const struct ui *ui);
int ui_height(const struct ui *ui);

/* The event loop: handles events, firing the timer, until the window is
 * closed, or ui_quit. Returns 0. */
int ui_run(struct ui *ui);
void ui_quit(struct ui *ui);
bool ui_quitting(const struct ui *ui);
/* Handles one event (as ui_run does for each it reads) and returns true;
 * false once the ui is quitting. Call ui_update afterwards. */
bool ui_handle(struct ui *ui, const struct win_event *e);
/* Lays out and draws what changed, and shows it. ui_run does this after
 * each batch of events. */
void ui_update(struct ui *ui);
void ui_on_key(struct ui *ui, ui_key_fn fn, void *arg);
void ui_on_close(struct ui *ui, ui_close_fn fn, void *arg);
/* Calls fn every `ms` milliseconds while ui_run runs; ms of 0 stops it. */
void ui_set_timer(struct ui *ui, int ms, ui_tick_fn fn, void *arg);
/* A message over the window, closed with Enter, Escape or its OK button;
 * nothing else works until then. */
void ui_message(struct ui *ui, const char *text);

/* --- containers --- */

/* Children are placed in a column / a row, `spacing` pixels apart; each
 * gets its natural size along the box, the room left over shared among
 * the children whose `expand` is set, and the box's whole width across it. */
struct ui_widget *ui_vbox(struct ui_widget *parent, int spacing);
struct ui_widget *ui_hbox(struct ui_widget *parent, int spacing);
/* Children fill a grid of `columns` equal cells, row by row. */
struct ui_widget *ui_grid(struct ui_widget *parent, int columns, int spacing);
void ui_set_padding(struct ui_widget *box, int pad); /* inside its edge */

/* --- widgets ---
 * Each is added at the end of `parent`. */

enum ui_align { UI_LEFT, UI_CENTER, UI_RIGHT };

struct ui_widget *ui_label(struct ui_widget *parent, const char *text);
/* For a label, button or checkbox: the font's size (1, 2, ...), and for
 * a label the alignment and the colour of its text. */
void ui_set_scale(struct ui_widget *w, int scale);
void ui_set_align(struct ui_widget *label, enum ui_align align);
void ui_set_text_color(struct ui_widget *label, gfx_color color);

/* fn runs when it is clicked (mouse released over it), or with Space or
 * Enter while it has the focus. */
struct ui_widget *ui_button(struct ui_widget *parent, const char *text, ui_fn fn, void *arg);

struct ui_widget *ui_checkbox(struct ui_widget *parent, const char *text, bool checked, ui_fn fn,
                              void *arg);
bool ui_checked(const struct ui_widget *checkbox);
void ui_set_checked(struct ui_widget *checkbox, bool checked);

/* One line of at most `max` characters; fn runs on Enter. Keys: the
 * printable ones, Backspace, Delete, Left, Right, Home, End, ^A, ^E, ^K,
 * ^U; the mouse places the caret. */
struct ui_widget *ui_entry(struct ui_widget *parent, const char *text, int max, ui_fn fn,
                           void *arg);

/* A column of rows. fn runs when a row is activated: Enter, or a double
 * click. A row's text may hold a Tab: what follows it is drawn dim, at
 * the right. */
struct ui_widget *ui_list(struct ui_widget *parent, ui_fn fn, void *arg);
int ui_list_add(struct ui_widget *list, const char *text, gfx_color color, void *data);
void ui_list_clear(struct ui_widget *list);
int ui_list_count(const struct ui_widget *list);
int ui_list_selected(const struct ui_widget *list); /* -1: none */
void ui_list_select(struct ui_widget *list, int index); /* scrolls to it */
const char *ui_list_text(const struct ui_widget *list, int index);
void *ui_list_data(const struct ui_widget *list, int index);

/* Several lines of text, with a scroll bar. Editable unless readonly:
 * the arrows, Home, End, PgUp, PgDn, Backspace, Delete, Enter, printable
 * keys; the mouse places the caret. Tab moves the focus on, not into the
 * text. A Tab character in the text shows as spaces to a multiple of 4. */
struct ui_widget *ui_text(struct ui_widget *parent, bool readonly);
void ui_text_numbers(struct ui_widget *text, bool on); /* line numbers in the margin */
int ui_text_lines(const struct ui_widget *text);
int ui_text_line(const struct ui_widget *text); /* the caret's line, from 0 */
int ui_text_column(const struct ui_widget *text);
int ui_text_top(const struct ui_widget *text); /* the first line shown */
void ui_text_goto(struct ui_widget *text, int line); /* caret to the line's start */

/* A bar filled value/max of the way. */
struct ui_widget *ui_progress(struct ui_widget *parent);
void ui_set_progress(struct ui_widget *bar, int value, int max);

struct ui_widget *ui_separator(struct ui_widget *parent); /* a thin line across the box */
struct ui_widget *ui_spacer(struct ui_widget *parent);    /* empty, and takes spare room */

/* A widget the program draws and handles itself. */
enum ui_mouse { UI_MOVE, UI_PRESS, UI_DRAG, UI_RELEASE };
struct ui_custom {
	/* Draw into c (clipped to the widget); w->r is where. */
	void (*paint)(struct ui_widget *w, struct gfx_canvas *c, void *arg);
	/* A key, while the widget has the focus; true if used. May be NULL. */
	bool (*key)(struct ui_widget *w, int key, void *arg);
	/* The pointer, relative to the widget, with the button mask (1: left,
	 * 2: right, 4: middle). PRESS, DRAG and RELEASE: for a press that began
	 * inside the widget, even once the pointer is out of it. May be NULL. */
	void (*mouse)(struct ui_widget *w, enum ui_mouse what, int x, int y, int buttons, void *arg);
	/* The widget was laid out at another size. May be NULL. */
	void (*resized)(struct ui_widget *w, void *arg);
	bool focusable;
};
struct ui_widget *ui_custom(struct ui_widget *parent, const struct ui_custom *ops, void *arg);

/* --- every widget --- */

/* The text of a label, button or checkbox (replaced), or the contents of
 * an entry or text widget; the pointer given back is the toolkit's, good
 * until the next change. */
void ui_set_text(struct ui_widget *w, const char *text);
const char *ui_get_text(const struct ui_widget *w);
void ui_on_change(struct ui_widget *w, ui_fn fn, void *arg); /* entry, text, list, checkbox */
void ui_set_expand(struct ui_widget *w, int weight);
/* Asks for at least this width and height (0 leaves one as it is). */
void ui_set_size(struct ui_widget *w, int width, int height);
void ui_set_enabled(struct ui_widget *w, bool enabled);
void ui_set_visible(struct ui_widget *w, bool visible);
/* Fills the widget with `bg` and draws a border of `border`, when not 0. */
void ui_set_colors(struct ui_widget *w, gfx_color bg, gfx_color border);
void ui_focus(struct ui_widget *w);
bool ui_has_focus(const struct ui_widget *w);
/* Whether Tab and a click can give it the focus (buttons, lists, text,
 * entries and checkboxes start as they do). */
void ui_set_focusable(struct ui_widget *w, bool focusable);
void ui_redraw(struct ui_widget *w); /* it changed: draw it again */
/* Only r (in window pixels) of it changed. */
void ui_redraw_rect(struct ui_widget *w, struct gfx_rect r);
void ui_destroy(struct ui_widget *w); /* with its children */

#endif
