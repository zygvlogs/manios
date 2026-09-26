/* Workspaces, layouts and the focus (DESIGN.md §3). Windows are kept in
 * one tiling order; each workspace shows its own windows in that order,
 * in one of three layouts:
 *
 *   grid  columns side by side, the windows shared out among them, the
 *         later columns taking one more when they don't divide evenly
 *   tall  the first window on the left, the others stacked on the right
 *   mono  the focused window alone, filling the screen
 *
 * A window is told its new size ("r W H") whenever its pane changes. */
#include "manide.h"
#include <string.h>

static const char *const LAYOUT_NAMES[LAYOUTS] = { "grid", "tall", "mono" };

const char *layout_name(enum layout l)
{
	return LAYOUT_NAMES[l];
}

struct gfx_rect tiling_area(void)
{
	return (struct gfx_rect){ GAP, BAR_H + GAP, D.width - 2 * GAP, D.height - BAR_H - 2 * GAP };
}

struct gfx_rect strip_rect(const struct window *w)
{
	return (struct gfx_rect){ w->pane.x + 1, w->pane.y + 1, w->pane.w - 2, HEAD_H - 1 };
}

struct gfx_rect close_rect(const struct window *w)
{
	struct gfx_rect s = strip_rect(w);
	return (struct gfx_rect){ s.x + s.w - CLOSE_W - 3, s.y, CLOSE_W + 3, s.h };
}

struct window *window_at(int x, int y)
{
	for (int i = 0; i < D.count; i++) {
		if (D.order[i]->shown && gfx_contains(D.order[i]->pane, x, y)) {
			return D.order[i];
		}
	}
	return NULL;
}

/* Splits `length` into n parts with `GAP` between them: part i's start
 * and size. The last part takes what division leaves over. */
static void share(int start, int length, int n, int i, int *at, int *size)
{
	int each = (length - (n - 1) * GAP) / n;
	*at = start + i * (each + GAP);
	*size = i == n - 1 ? start + length - *at : each;
}

static void place(struct window *w, struct gfx_rect pane)
{
	w->shown = true;
	w->pane = pane;
	w->content = (struct gfx_rect){ pane.x + 1, pane.y + HEAD_H, pane.w - 2, pane.h - HEAD_H - 1 };
	if (w->content.w < MIN_SIZE) {
		w->content.w = MIN_SIZE;
	}
	if (w->content.h < MIN_SIZE) {
		w->content.h = MIN_SIZE;
	}
	if (w->content.w != w->told_w || w->content.h != w->told_h) {
		w->told_w = w->content.w;
		w->told_h = w->content.h;
		post_event(w, "r %d %d", w->told_w, w->told_h);
	}
}

void retile(void)
{
	struct window *list[MAX_WINDOWS];
	int n = 0;
	for (int i = 0; i < D.count; i++) {
		D.order[i]->shown = false;
		if (D.order[i]->ws == D.ws) {
			list[n++] = D.order[i];
		}
	}
	struct gfx_rect a = tiling_area();
	struct workspace *ws = &D.wss[D.ws];
	if (n == 0) {
		/* nothing to place */
	} else if (ws->layout == LAYOUT_MONO || n == 1) {
		place(D.focus && D.focus->ws == D.ws ? D.focus : list[0], a);
	} else if (ws->layout == LAYOUT_TALL) {
		int master_w = (a.w - GAP) * ws->master / 100;
		place(list[0], (struct gfx_rect){ a.x, a.y, master_w, a.h });
		for (int i = 1; i < n; i++) {
			int y, h;
			share(a.y, a.h, n - 1, i - 1, &y, &h);
			place(list[i], (struct gfx_rect){ a.x + master_w + GAP, y, a.w - master_w - GAP, h });
		}
	} else {
		int cols = 1;
		while (cols * cols < n) {
			cols++;
		}
		int k = 0;
		for (int c = 0; c < cols; c++) {
			int rows = n / cols + (c >= cols - n % cols ? 1 : 0);
			int x, w;
			share(a.x, a.w, cols, c, &x, &w);
			for (int r = 0; r < rows; r++) {
				int y, h;
				share(a.y, a.h, rows, r, &y, &h);
				place(list[k++], (struct gfx_rect){ x, y, w, h });
			}
		}
	}
	damage(a);
	damage_bar();
}

void set_focus(struct window *w)
{
	if (D.focus == w) {
		return;
	}
	struct window *old = D.focus;
	D.focus = w;
	if (old) {
		post_event(old, "f 0");
		if (old->shown) {
			damage(old->pane);
		}
	}
	if (w) {
		D.wss[w->ws].focus = w;
		post_event(w, "f 1");
		if (w->shown) {
			damage(w->pane);
		}
	}
	if (D.wss[D.ws].layout == LAYOUT_MONO) {
		retile(); /* the focused window is the one shown */
	}
	damage_bar();
}

static int index_of(const struct window *w)
{
	for (int i = 0; i < D.count; i++) {
		if (D.order[i] == w) {
			return i;
		}
	}
	return -1;
}

/* The window `delta` places from w among those on its workspace. */
static struct window *step_from(struct window *w, int delta)
{
	int i = index_of(w);
	for (int k = 1; k < D.count; k++) {
		struct window *c = D.order[(i + delta * k + D.count * k) % D.count];
		if (c->ws == w->ws) {
			return c;
		}
	}
	return w;
}

void focus_step(int delta)
{
	if (D.focus) {
		set_focus(step_from(D.focus, delta));
	}
}

void swap_step(int delta)
{
	if (!D.focus) {
		return;
	}
	struct window *other = step_from(D.focus, delta);
	int a = index_of(D.focus), b = index_of(other);
	D.order[a] = other;
	D.order[b] = D.focus;
	retile();
}

/* The window to focus on workspace ws: the one it last had. */
static struct window *workspace_focus(int ws)
{
	struct window *f = D.wss[ws].focus;
	if (f && index_of(f) >= 0 && f->ws == ws) {
		return f;
	}
	for (int i = 0; i < D.count; i++) {
		if (D.order[i]->ws == ws) {
			return D.order[i];
		}
	}
	return NULL;
}

void show_workspace(int ws)
{
	if (ws == D.ws) {
		return;
	}
	D.ws = ws;
	D.place_ws = ws;
	set_focus(workspace_focus(ws));
	retile();
	say("workspace %d", ws + 1);
}

void send_to_workspace(struct window *w, int ws)
{
	if (w->ws == ws) {
		return;
	}
	int from = w->ws;
	w->ws = ws;
	if (D.wss[from].focus == w) {
		D.wss[from].focus = NULL;
	}
	if (!D.wss[ws].focus) {
		D.wss[ws].focus = w;
	}
	if (D.focus == w) {
		set_focus(workspace_focus(D.ws));
	}
	retile();
	say("window %d to workspace %d", w->id, ws + 1);
}

void next_layout(void)
{
	struct workspace *ws = &D.wss[D.ws];
	ws->layout = (enum layout)((ws->layout + 1) % LAYOUTS);
	retile();
	say("layout %s", layout_name(ws->layout));
}

void grow_master(int delta)
{
	struct workspace *ws = &D.wss[D.ws];
	int m = ws->master + delta;
	ws->master = m < 20 ? 20 : m > 80 ? 80 : m;
	if (ws->layout == LAYOUT_TALL) {
		retile();
	}
}

/* A new window: the last in the order, on the workspace for new ones,
 * with the focus if that workspace is in view. */
void add_window(struct window *w)
{
	w->ws = D.place_ws;
	D.order[D.count++] = w;
	if (w->ws == D.ws) {
		set_focus(w);
	} else if (!D.wss[w->ws].focus) {
		D.wss[w->ws].focus = w;
	}
	retile();
}

void remove_window(struct window *w)
{
	int i = index_of(w);
	struct window *next = D.count > 1 ? step_from(w, -1) : NULL;
	memmove(&D.order[i], &D.order[i + 1], (size_t)(D.count - i - 1) * sizeof(D.order[0]));
	D.count--;
	if (next == w || (next && next->ws != w->ws)) {
		next = NULL;
	}
	if (D.wss[w->ws].focus == w) {
		D.wss[w->ws].focus = next;
	}
	if (D.focus == w) {
		D.focus = NULL; /* it hears nothing more */
		set_focus(next);
	}
	retile();
}
