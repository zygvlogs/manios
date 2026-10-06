/* The scroll bar the list and the text widget share: a track at the right
 * edge of an area, with a thumb as tall as the visible part is of the
 * whole. Everything is in the same coordinates as `area`. */
#include "ui_priv.h"

#define MIN_THUMB 12

struct gfx_rect ui_scroll_bar(struct gfx_rect area)
{
	return (struct gfx_rect){ area.x + area.w - UI_BAR_W, area.y, UI_BAR_W, area.h };
}

/* The thumb; empty when everything is visible. */
static struct gfx_rect thumb(struct gfx_rect bar, int total, int visible, int top)
{
	if (total <= visible || total <= 0 || bar.h <= 0) {
		return (struct gfx_rect){ 0, 0, 0, 0 };
	}
	int h = ui_imin(bar.h, ui_imax(MIN_THUMB, bar.h * visible / total));
	int y = bar.y + (bar.h - h) * top / (total - visible);
	return (struct gfx_rect){ bar.x + 1, y, bar.w - 2, h };
}

void ui_scroll_paint(struct gfx_canvas *c, struct gfx_rect area, int total, int visible, int top)
{
	struct gfx_rect bar = ui_scroll_bar(area);
	gfx_fill(c, bar, ui_theme.panel);
	gfx_vline(c, bar.x, bar.y, bar.h, ui_theme.edge);
	struct gfx_rect t = thumb(bar, total, visible, top);
	if (!gfx_rect_empty(t)) {
		gfx_fill(c, t, ui_theme.dim);
	}
}

int ui_scroll_mouse(struct ui_scroll *s, struct gfx_rect area, int total, int visible, int top,
                    enum ui_mouse what, int x, int y)
{
	struct gfx_rect bar = ui_scroll_bar(area), t = thumb(bar, total, visible, top);
	int last = ui_imax(0, total - visible);
	if (what == UI_PRESS) {
		s->dragging = -1;
		if (!gfx_contains(bar, x, y)) {
			return -1;
		}
		if (gfx_rect_empty(t)) {
			return top; /* nothing to scroll */
		}
		if (gfx_contains(t, x, y)) {
			s->dragging = y - t.y;
			return top;
		}
		return ui_iclamp(y < t.y ? top - visible : top + visible, 0, last);
	}
	if (what == UI_DRAG && s->dragging >= 0) {
		int range = bar.h - t.h;
		if (range <= 0 || gfx_rect_empty(t)) {
			return top;
		}
		int at = y - s->dragging - bar.y;
		return ui_iclamp((at * last + range / 2) / range, 0, last);
	}
	if (what == UI_RELEASE) {
		bool was = s->dragging >= 0;
		s->dragging = -1;
		return was ? top : -1;
	}
	return -1;
}
