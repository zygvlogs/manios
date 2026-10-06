/* libui's core (ui.h): windows, the event loop, the widget tree, drawing,
 * focus, and the message that goes over the window. */
#include "ui_priv.h"
#include <errno.h>
#include <manios.h>
#include <stdlib.h>
#include <string.h>

const struct ui_theme ui_theme = {
	.bg = GFX_RGB(0x0F, 0x13, 0x19),
	.panel = GFX_RGB(0x1A, 0x20, 0x2A),
	.panel_hot = GFX_RGB(0x24, 0x2C, 0x39),
	.field = GFX_RGB(0x0A, 0x0D, 0x12),
	.edge = GFX_RGB(0x2A, 0x31, 0x3C),
	.text = GFX_RGB(0xD8, 0xDE, 0xE9),
	.dim = GFX_RGB(0x6B, 0x74, 0x83),
	.focus = GFX_RGB(0x3F, 0xC8, 0xD8),
	.accent = GFX_RGB(0xE0, 0x7A, 0x2E),
	.select = GFX_RGB(0x1F, 0x3A, 0x45),
};

#define MESSAGE_PAD 12
#define TIMER_HALF 0x40000000u /* uptime_ms() wraps modulo 2^31 */

/* --- the tree --- */

static bool hidden(const struct ui_widget *w)
{
	for (; w; w = w->parent) {
		if (w->flags & UI_F_HIDDEN) {
			return true;
		}
	}
	return false;
}

static bool within(const struct ui_widget *w, const struct ui_widget *ancestor)
{
	for (; w; w = w->parent) {
		if (w == ancestor) {
			return true;
		}
	}
	return false;
}

void *ui_new_widget(struct ui_widget *parent, const struct ui_class *cls, size_t size,
                    unsigned flags)
{
	if (!parent) {
		return NULL;
	}
	struct ui_widget *w = calloc(1, size);
	if (!w) {
		return NULL;
	}
	w->cls = cls;
	w->flags = flags;
	w->ui = parent->ui;
	w->parent = parent;
	if (parent->last) {
		parent->last->next = w;
	} else {
		parent->first = w;
	}
	parent->last = w;
	ui_relayout(w->ui);
	return w;
}

static void free_tree(struct ui_widget *w)
{
	for (struct ui_widget *c = w->first, *next; c; c = next) {
		next = c->next;
		free_tree(c);
	}
	if (w->cls->destroy) {
		w->cls->destroy(w);
	}
	free(w);
}

void ui_destroy(struct ui_widget *w)
{
	if (!w || !w->parent) {
		return; /* the root goes with the ui */
	}
	struct ui *ui = w->ui;
	struct ui_widget *p = w->parent;
	if (p->first == w) {
		p->first = w->next;
	}
	for (struct ui_widget *s = p->first; s; s = s->next) {
		if (s->next == w) {
			s->next = w->next;
		}
	}
	if (p->last == w) {
		p->last = NULL;
		for (struct ui_widget *s = p->first; s; s = s->next) {
			p->last = s;
		}
	}
	if (within(ui->focus, w)) {
		ui->focus = NULL;
	}
	if (within(ui->hover, w)) {
		ui->hover = NULL;
	}
	if (within(ui->grab, w)) {
		ui->grab = NULL;
	}
	if (within(ui->before_modal, w)) {
		ui->before_modal = NULL; /* a message is up over it */
	}
	free_tree(w);
	ui_relayout(ui);
}

static struct ui_widget *preorder_next(const struct ui_widget *w)
{
	if (w->first) {
		return w->first;
	}
	for (; w; w = w->parent) {
		if (w->next) {
			return w->next;
		}
	}
	return NULL;
}

static bool can_focus(const struct ui_widget *w)
{
	return (w->flags & UI_F_FOCUSABLE) && !(w->flags & UI_F_DISABLED) && !hidden(w);
}

static void set_focus(struct ui *ui, struct ui_widget *w)
{
	if (ui->focus == w) {
		return;
	}
	if (ui->focus) {
		ui_redraw(ui->focus);
	}
	ui->focus = w;
	if (w) {
		ui_redraw(w);
	}
}

/* The next widget that takes the focus after the focused one, going
 * round; inside the message, only the message's own. */
static void focus_step(struct ui *ui)
{
	struct ui_widget *top = ui->modal ? ui->modal : ui->root;
	struct ui_widget *w = ui->focus && within(ui->focus, top) ? ui->focus : top;
	for (int guard = 0; guard < 100000; guard++) {
		w = preorder_next(w);
		if (!w || !within(w, top)) {
			w = top;
		}
		if (can_focus(w)) {
			set_focus(ui, w);
			return;
		}
		if (w == (ui->focus ? ui->focus : top) && !can_focus(w)) {
			break; /* all the way round: nothing takes it */
		}
	}
}

void ui_focus(struct ui_widget *w)
{
	if (w && can_focus(w) && !(w->ui->modal && !within(w, w->ui->modal))) {
		set_focus(w->ui, w);
	}
}

bool ui_has_focus(const struct ui_widget *w)
{
	return w && w->ui->focus == w;
}

bool ui_is_focused(const struct ui_widget *w)
{
	return w->ui->focus == w && w->ui->active;
}

/* --- drawing --- */

static void add_dirty(struct ui *ui, struct gfx_rect r)
{
	if (gfx_rect_empty(r)) {
		return;
	}
	if (gfx_rect_empty(ui->dirty)) {
		ui->dirty = r;
		return;
	}
	int x0 = ui_imin(ui->dirty.x, r.x), y0 = ui_imin(ui->dirty.y, r.y);
	int x1 = ui_imax(ui->dirty.x + ui->dirty.w, r.x + r.w);
	int y1 = ui_imax(ui->dirty.y + ui->dirty.h, r.y + r.h);
	ui->dirty = (struct gfx_rect){ x0, y0, x1 - x0, y1 - y0 };
}

void ui_redraw(struct ui_widget *w)
{
	if (w && !hidden(w)) {
		add_dirty(w->ui, w->r);
	}
}

void ui_redraw_rect(struct ui_widget *w, struct gfx_rect r)
{
	if (w && !hidden(w)) {
		add_dirty(w->ui, gfx_intersect(r, w->r));
	}
}

void ui_set_focusable(struct ui_widget *w, bool focusable)
{
	if (!w) {
		return;
	}
	w->flags = focusable ? w->flags | UI_F_FOCUSABLE : w->flags & ~UI_F_FOCUSABLE;
	if (!focusable && w->ui->focus == w) {
		w->ui->focus = NULL;
		ui_relayout(w->ui); /* the next layout gives the focus to another */
	}
}

void ui_relayout(struct ui *ui)
{
	if (ui) {
		ui->need_layout = true;
	}
}

void ui_draw_frame(struct ui_widget *w, struct gfx_canvas *c, gfx_color fill)
{
	if (fill) {
		gfx_fill(c, w->r, fill);
	}
	gfx_outline(c, w->r, ui_is_focused(w) ? ui_theme.focus : ui_theme.edge);
}

static void paint(struct ui_widget *w, struct gfx_canvas *c)
{
	if (w->flags & UI_F_HIDDEN) {
		return;
	}
	struct gfx_rect saved = c->clip, mine = gfx_intersect(saved, w->r);
	if (gfx_rect_empty(mine)) {
		return;
	}
	gfx_set_clip(c, mine);
	if (w->bg) {
		gfx_fill(c, w->r, w->bg);
	}
	if (w->cls->paint) {
		w->cls->paint(w, c);
	}
	if (w->border) {
		gfx_outline(c, w->r, w->border);
	}
	for (struct ui_widget *k = w->first; k; k = k->next) {
		paint(k, c);
	}
	c->clip = saved;
}

/* --- layout --- */

static void measure(struct ui_widget *w)
{
	if (w->flags & UI_F_HIDDEN) {
		return;
	}
	for (struct ui_widget *k = w->first; k; k = k->next) {
		measure(k);
	}
	w->want_w = w->want_h = 0;
	if (w->cls->measure) {
		w->cls->measure(w);
	}
	w->want_w = ui_imax(w->want_w, w->min_w);
	w->want_h = ui_imax(w->want_h, w->min_h);
}

static void place(struct ui_widget *w)
{
	if (w->flags & UI_F_HIDDEN) {
		return;
	}
	if (w->cls->layout) {
		w->cls->layout(w);
	}
	for (struct ui_widget *k = w->first; k; k = k->next) {
		place(k);
	}
}

static void do_layout(struct ui *ui)
{
	measure(ui->root);
	ui->root->r = (struct gfx_rect){ 0, 0, ui->width, ui->height };
	place(ui->root);
	if (ui->modal) {
		measure(ui->modal);
		int w = ui_imin(ui->modal->want_w, ui->width - 16);
		int h = ui_imin(ui->modal->want_h, ui->height - 16);
		ui->modal->r = (struct gfx_rect){ (ui->width - w) / 2, (ui->height - h) / 2, w, h };
		place(ui->modal);
	}
	if (!ui->focus) {
		struct ui_widget *top = ui->modal ? ui->modal : ui->root;
		for (struct ui_widget *w = top; w; w = preorder_next(w)) {
			if (can_focus(w)) {
				ui->focus = w;
				break;
			}
		}
	}
}

struct gfx_canvas *ui_canvas_of(struct ui *ui)
{
	return ui->win ? ui->win->canvas : ui->own;
}

void ui_update(struct ui *ui)
{
	struct gfx_canvas *c = ui_canvas_of(ui);
	if (ui->need_layout) {
		ui->need_layout = false;
		do_layout(ui);
		ui->dirty = (struct gfx_rect){ 0, 0, c->width, c->height };
	}
	struct gfx_rect d = gfx_intersect(ui->dirty, (struct gfx_rect){ 0, 0, c->width, c->height });
	ui->dirty = (struct gfx_rect){ 0, 0, 0, 0 };
	if (gfx_rect_empty(d)) {
		return;
	}
	gfx_set_clip(c, d);
	gfx_fill(c, d, ui_theme.bg);
	paint(ui->root, c);
	if (ui->modal) {
		gfx_fill(c, d, GFX_RGBA(0, 0, 0, 0xB0)); /* dims what is under it */
		paint(ui->modal, c);
	}
	gfx_reset_clip(c);
	if (ui->win) {
		win_flush(ui->win, d.y, d.h);
	}
}

/* --- the message --- */

static void close_message(struct ui *ui)
{
	if (!ui->modal) {
		return;
	}
	struct ui_widget *m = ui->modal;
	ui->modal = NULL;
	if (within(ui->focus, m)) {
		/* Back to where it was, if that can still take it; else the next layout chooses. */
		ui->focus = ui->before_modal && can_focus(ui->before_modal) ? ui->before_modal : NULL;
	}
	if (within(ui->hover, m)) {
		ui->hover = NULL;
	}
	if (within(ui->grab, m)) {
		ui->grab = NULL;
	}
	ui->before_modal = NULL;
	free_tree(m);
	ui_relayout(ui);
}

static void message_ok(struct ui_widget *w, void *arg)
{
	(void)w;
	close_message(arg);
}

void ui_message(struct ui *ui, const char *text)
{
	close_message(ui);
	struct ui_widget *m = ui_box_new(ui, NULL, &ui_vbox_class, 10, 0);
	if (!m) {
		return;
	}
	ui_set_padding(m, MESSAGE_PAD);
	ui_set_colors(m, ui_theme.panel, ui_theme.focus);
	struct ui_widget *label = ui_label(m, text);
	ui_set_align(label, UI_CENTER);
	struct ui_widget *ok = ui_button(m, "OK", message_ok, ui);
	if (!label || !ok) {
		free_tree(m);
		return;
	}
	ui->before_modal = ui->focus;
	ui->modal = m;
	ui->focus = ok;
	ui->hover = ui->grab = NULL;
	ui_relayout(ui);
}

/* --- events --- */

static void key(struct ui *ui, int k)
{
	if (ui->modal) {
		if (k == 0x1B || k == '\n' || k == '\r' || k == ' ') {
			close_message(ui);
		}
		return;
	}
	if (ui->key_fn && ui->key_fn(ui, k, ui->key_arg)) {
		return;
	}
	struct ui_widget *f = ui->focus;
	if (f && ui_is_enabled(f) && f->cls->key && f->cls->key(f, k)) {
		return;
	}
	if (k == '\t') {
		focus_step(ui);
	}
}

/* The deepest widget at (x, y) in w's tree. */
static struct ui_widget *hit(struct ui_widget *w, int x, int y)
{
	if ((w->flags & UI_F_HIDDEN) || !gfx_contains(w->r, x, y)) {
		return NULL;
	}
	struct ui_widget *best = w;
	for (struct ui_widget *k = w->first; k; k = k->next) {
		struct ui_widget *h = hit(k, x, y);
		if (h) {
			best = h; /* later children are drawn over earlier ones */
		}
	}
	return best;
}

static void send_mouse(struct ui_widget *w, enum ui_mouse what, int x, int y, int buttons)
{
	if (w->cls->mouse && ui_is_enabled(w)) {
		w->cls->mouse(w, what, x - w->r.x, y - w->r.y, buttons);
	}
}

static void set_hover(struct ui *ui, struct ui_widget *w)
{
	if (ui->hover != w) {
		struct ui_widget *old = ui->hover;
		ui->hover = w;
		ui_redraw(old);
		ui_redraw(w);
	}
}

static void mouse(struct ui *ui, int x, int y, int buttons)
{
	int pressed = buttons & ~ui->buttons;
	ui->buttons = buttons;
	if (ui->grab) {
		struct ui_widget *g = ui->grab;
		if (!buttons) {
			ui->grab = NULL;
			send_mouse(g, UI_RELEASE, x, y, 0);
			set_hover(ui, hit(ui->modal ? ui->modal : ui->root, x, y));
		} else {
			send_mouse(g, UI_DRAG, x, y, buttons); /* another button counts as a drag too */
		}
		return;
	}
	struct ui_widget *h = hit(ui->modal ? ui->modal : ui->root, x, y);
	set_hover(ui, h);
	if (!h) {
		return;
	}
	if (pressed && ui_is_enabled(h)) {
		if (can_focus(h)) {
			set_focus(ui, h);
		}
		ui->grab = h;
		send_mouse(h, UI_PRESS, x, y, buttons);
	} else if (!buttons) {
		send_mouse(h, UI_MOVE, x, y, 0);
	}
}

static void resize(struct ui *ui, int width, int height)
{
	if (ui->win) {
		width = ui->win->width; /* libwin has already taken the new size */
		height = ui->win->height;
	} else if (width != ui->width || height != ui->height) {
		struct gfx_canvas *c = gfx_canvas_new(width, height);
		if (!c) {
			return;
		}
		gfx_canvas_free(ui->own);
		ui->own = c;
	}
	ui->width = width;
	ui->height = height;
	ui_relayout(ui);
}

bool ui_handle(struct ui *ui, const struct win_event *e)
{
	switch (e->type) {
	case WIN_KEY:
		if (!e->alt) {
			key(ui, e->key);
		}
		break;
	case WIN_MOUSE:
		mouse(ui, e->x, e->y, e->buttons);
		break;
	case WIN_FOCUS:
		ui->active = e->focused != 0;
		if (!ui->active) {
			set_hover(ui, NULL);
		}
		ui_redraw(ui->focus);
		break;
	case WIN_CLOSE:
		if (!ui->close_fn || ui->close_fn(ui, ui->close_arg)) {
			ui->quit = true;
		}
		break;
	case WIN_RESIZE:
		resize(ui, e->width, e->height);
		break;
	}
	return !ui->quit;
}

static void fire_timer(struct ui *ui)
{
	if (ui->timer_ms <= 0 || ui->quit) {
		return;
	}
	uint32_t now = uptime_ms();
	if (((now - ui->timer_next) & 0x7FFFFFFFu) < TIMER_HALF) {
		ui->timer_next = now + (uint32_t)ui->timer_ms;
		if (ui->timer_fn) {
			ui->timer_fn(ui, ui->timer_arg);
		}
	}
}

int ui_run(struct ui *ui)
{
	if (!ui->win) {
		return -1;
	}
	ui_update(ui);
	while (!ui->quit) {
		int wait = -1;
		if (ui->timer_ms > 0) {
			uint32_t left = (ui->timer_next - uptime_ms()) & 0x7FFFFFFFu;
			wait = left < TIMER_HALF ? (int)left : 0;
		}
		struct win_event e;
		int got = win_next(ui->win, &e, wait);
		if (got < 0) {
			break; /* the window is gone */
		}
		if (got > 0) {
			ui_handle(ui, &e);
			/* Whatever else is waiting is handled before anything is drawn. */
			while (!ui->quit && (got = win_next(ui->win, &e, 0)) > 0) {
				ui_handle(ui, &e);
			}
			if (got < 0) {
				break;
			}
		}
		fire_timer(ui);
		ui_update(ui);
	}
	return 0;
}

void ui_quit(struct ui *ui)
{
	ui->quit = true;
}

bool ui_quitting(const struct ui *ui)
{
	return ui->quit;
}

void ui_on_key(struct ui *ui, ui_key_fn fn, void *arg)
{
	ui->key_fn = fn;
	ui->key_arg = arg;
}

void ui_on_close(struct ui *ui, ui_close_fn fn, void *arg)
{
	ui->close_fn = fn;
	ui->close_arg = arg;
}

void ui_set_timer(struct ui *ui, int ms, ui_tick_fn fn, void *arg)
{
	ui->timer_ms = ms > 0 ? ms : 0;
	ui->timer_fn = fn;
	ui->timer_arg = arg;
	ui->timer_next = uptime_ms() + (uint32_t)ui->timer_ms;
}

/* --- making and freeing a ui --- */

static struct ui *make(struct win *win, int width, int height)
{
	struct ui *ui = calloc(1, sizeof(*ui));
	if (!ui) {
		errno = ENOMEM;
		return NULL;
	}
	ui->win = win;
	ui->active = true;
	if (!win) {
		ui->own = gfx_canvas_new(width, height);
		if (!ui->own) {
			free(ui);
			errno = ENOMEM;
			return NULL;
		}
	}
	ui->width = win ? win->width : width;
	ui->height = win ? win->height : height;
	ui->root = ui_box_new(ui, NULL, &ui_vbox_class, 6, 0);
	if (!ui->root) {
		gfx_canvas_free(ui->own);
		free(ui);
		errno = ENOMEM;
		return NULL;
	}
	ui_set_padding(ui->root, 6);
	ui->need_layout = true;
	return ui;
}

struct ui *ui_open(const char *title, int width, int height)
{
	struct win *win = win_open(title, width, height);
	if (!win) {
		return NULL;
	}
	struct ui *ui = make(win, width, height);
	if (!ui) {
		int err = errno;
		win_close(win);
		errno = err;
	}
	return ui;
}

struct ui *ui_new(int width, int height)
{
	return make(NULL, width, height);
}

void ui_free(struct ui *ui)
{
	if (!ui) {
		return;
	}
	if (ui->modal) {
		free_tree(ui->modal);
	}
	free_tree(ui->root);
	if (ui->win) {
		win_close(ui->win);
	}
	gfx_canvas_free(ui->own);
	free(ui);
}

struct ui_widget *ui_root(struct ui *ui)
{
	return ui->root;
}

void ui_set_title(struct ui *ui, const char *title)
{
	if (ui->win) {
		win_set_title(ui->win, title);
	}
}

int ui_width(const struct ui *ui)
{
	return ui->width;
}

int ui_height(const struct ui *ui)
{
	return ui->height;
}

/* --- every widget --- */

bool ui_copy_string(char **dst, const char *s)
{
	char *copy = strdup(s ? s : "");
	if (!copy) {
		return false;
	}
	free(*dst);
	*dst = copy;
	return true;
}

void ui_set_text(struct ui_widget *w, const char *text)
{
	if (w && w->cls->set_text) {
		w->cls->set_text(w, text);
	}
}

const char *ui_get_text(const struct ui_widget *w)
{
	return w && w->cls->get_text ? w->cls->get_text(w) : "";
}

void ui_on_change(struct ui_widget *w, ui_fn fn, void *arg)
{
	if (w) {
		w->change = fn;
		w->change_arg = arg;
	}
}

void ui_set_expand(struct ui_widget *w, int weight)
{
	if (w) {
		w->expand = weight < 0 ? 0 : weight;
		ui_relayout(w->ui);
	}
}

void ui_set_size(struct ui_widget *w, int width, int height)
{
	if (w) {
		w->min_w = width > 0 ? width : w->min_w;
		w->min_h = height > 0 ? height : w->min_h;
		ui_relayout(w->ui);
	}
}

void ui_set_enabled(struct ui_widget *w, bool enabled)
{
	if (w && ui_is_enabled(w) != enabled) {
		w->flags = enabled ? w->flags & ~UI_F_DISABLED : w->flags | UI_F_DISABLED;
		if (!enabled && w->ui->focus == w) {
			focus_step(w->ui);
			if (w->ui->focus == w) {
				w->ui->focus = NULL;
			}
		}
		ui_redraw(w);
	}
}

void ui_set_visible(struct ui_widget *w, bool visible)
{
	if (w && (!(w->flags & UI_F_HIDDEN)) != visible) {
		w->flags = visible ? w->flags & ~UI_F_HIDDEN : w->flags | UI_F_HIDDEN;
		if (!visible) {
			if (within(w->ui->focus, w)) {
				w->ui->focus = NULL;
			}
			if (within(w->ui->hover, w)) {
				w->ui->hover = NULL;
			}
			if (within(w->ui->grab, w)) {
				w->ui->grab = NULL;
			}
		}
		ui_relayout(w->ui);
	}
}

void ui_set_colors(struct ui_widget *w, gfx_color bg, gfx_color border)
{
	if (w) {
		w->bg = bg;
		w->border = border;
		ui_relayout(w->ui); /* a label's padding depends on them */
	}
}
