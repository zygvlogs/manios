/* Composition (DESIGN.md §3, §6). Everything is drawn into the back
 * buffer, limited to the damaged rectangle, bottom layer first, and that
 * rectangle is then copied to the screen. */
#include "desktop.h"
#include <manios.h>
#include <stdio.h>
#include <string.h>

#define MENU_W 160
#define ITEM_H 20
#define SEPARATOR_H 7
#define BUTTONS_X 78
#define BUTTON_W_MAX 120

/* The pointer: X outline, . fill; the hot spot is the top-left. */
static const char *const CURSOR[] = {
	"X",
	"XX",
	"X.X",
	"X..X",
	"X...X",
	"X....X",
	"X.....X",
	"X......X",
	"X.......X",
	"X........X",
	"X.....XXXXX",
	"X..X..X",
	"X.X X..X",
	"XX  X..X",
	"X    X..X",
	"     X..X",
	"      XX",
};
#define CURSOR_W 11
#define CURSOR_H 17

void damage(struct gfx_rect r)
{
	r = gfx_intersect(r, (struct gfx_rect){ 0, 0, D.width, D.height });
	if (gfx_rect_empty(r)) {
		return;
	}
	if (gfx_rect_empty(D.damage)) {
		D.damage = r;
		return;
	}
	int x0 = r.x < D.damage.x ? r.x : D.damage.x;
	int y0 = r.y < D.damage.y ? r.y : D.damage.y;
	int x1 = r.x + r.w > D.damage.x + D.damage.w ? r.x + r.w : D.damage.x + D.damage.w;
	int y1 = r.y + r.h > D.damage.y + D.damage.h ? r.y + r.h : D.damage.y + D.damage.h;
	D.damage = (struct gfx_rect){ x0, y0, x1 - x0, y1 - y0 };
}

void damage_all(void)
{
	damage((struct gfx_rect){ 0, 0, D.width, D.height });
}

/* --- geometry --- */

struct gfx_rect frame_rect(const struct window *w)
{
	return (struct gfx_rect){ w->content.x - BORDER, w->content.y - TITLE_H - BORDER,
		                      w->content.w + 2 * BORDER, w->content.h + TITLE_H + 2 * BORDER };
}

struct gfx_rect title_rect(const struct window *w)
{
	return (struct gfx_rect){ w->content.x, w->content.y - TITLE_H, w->content.w, TITLE_H };
}

struct gfx_rect close_rect(const struct window *w)
{
	struct gfx_rect t = title_rect(w);
	return (struct gfx_rect){ t.x + t.w - CLOSE_BOX - 3, t.y + (TITLE_H - CLOSE_BOX) / 2, CLOSE_BOX,
		                      CLOSE_BOX };
}

struct gfx_rect cursor_rect(void)
{
	return (struct gfx_rect){ D.px, D.py, CURSOR_W, CURSOR_H };
}

struct gfx_rect menu_button_rect(void)
{
	return (struct gfx_rect){ 0, 0, BUTTONS_X - 4, PANEL_H - 1 };
}

struct gfx_rect menu_item_rect(int i)
{
	int y = PANEL_H + 2;
	for (int k = 0; k < i; k++) {
		y += MENU[k].label ? ITEM_H : SEPARATOR_H;
	}
	return (struct gfx_rect){ 1, y, MENU_W - 2, MENU[i].label ? ITEM_H : SEPARATOR_H };
}

struct gfx_rect menu_rect(void)
{
	struct gfx_rect last = menu_item_rect(MENU_COUNT - 1);
	return (struct gfx_rect){ 0, PANEL_H - 1, MENU_W, last.y + last.h + 2 - (PANEL_H - 1) };
}

int windows_by_id(struct window **out)
{
	int n = 0;
	for (int i = 0; i < D.count; i++) {
		int k = n++;
		while (k > 0 && out[k - 1]->id > D.stack[i]->id) {
			out[k] = out[k - 1];
			k--;
		}
		out[k] = D.stack[i];
	}
	return n;
}

struct gfx_rect window_button_rect(int i, int n)
{
	int avail = D.width - BUTTONS_X - 70; /* the clock's room */
	int w = n ? avail / n : 0;
	if (w > BUTTON_W_MAX) {
		w = BUTTON_W_MAX;
	}
	return (struct gfx_rect){ BUTTONS_X + i * w, 3, w - 3, PANEL_H - 7 };
}

/* --- drawing --- */

/* Text cut to fit `width` pixels. */
static void label(struct gfx_canvas *c, int x, int y, int width, const char *s, gfx_color color)
{
	char buf[TITLE_MAX + 1];
	int fit = width / GFX_CELL_W;
	if (fit <= 0) {
		return;
	}
	strlcpy(buf, s, sizeof(buf));
	if ((int)strlen(buf) > fit) {
		buf[fit] = '\0';
	}
	gfx_text(c, x, y, buf, color, 1);
}

static void draw_background(struct gfx_canvas *c)
{
	gfx_gradient(c, (struct gfx_rect){ 0, 0, D.width, D.height }, C_BG_TOP, C_BG_BOTTOM);
	/* The name in large, faint type. */
	const char *name = "ManiOS", *sub = "ZygKernel Technology";
	int scale = D.width >= 800 ? 8 : 6;
	int w = gfx_text_width(name, scale), sw = gfx_text_width(sub, 2);
	int y = (D.height - GFX_CELL_H * scale) / 2;
	gfx_text(c, (D.width - w) / 2, y, name, GFX_RGBA(255, 255, 255, 20), scale);
	gfx_text(c, (D.width - sw) / 2, y + GFX_CELL_H * scale + 6, sub, GFX_RGBA(255, 255, 255, 28), 2);
}

static void draw_window(struct gfx_canvas *c, struct window *w)
{
	bool focused = D.focus == w;
	struct gfx_rect t = title_rect(w), x = close_rect(w);
	gfx_outline(c, frame_rect(w), C_FRAME);
	gfx_fill(c, t, focused ? C_ACCENT : C_INACTIVE);
	label(c, t.x + 6, t.y + (TITLE_H - GFX_CELL_H) / 2 + 1, x.x - t.x - 10, w->title,
	      focused ? C_DARK_TEXT : C_LIGHT_TEXT);
	/* The close box: a square with a cross. */
	gfx_fill(c, x, focused ? GFX_RGB(0xB8, 0x5E, 0x1C) : GFX_RGB(0x48, 0x4F, 0x5B));
	gfx_line(c, x.x + 3, x.y + 3, x.x + x.w - 4, x.y + x.h - 4, focused ? C_DARK_TEXT : C_LIGHT_TEXT);
	gfx_line(c, x.x + x.w - 4, x.y + 3, x.x + 3, x.y + x.h - 4, focused ? C_DARK_TEXT : C_LIGHT_TEXT);
	gfx_blit(c, w->content.x, w->content.y, w->image,
	         (struct gfx_rect){ 0, 0, w->content.w, w->content.h });
}

static void draw_panel(struct gfx_canvas *c)
{
	gfx_fill(c, (struct gfx_rect){ 0, 0, D.width, PANEL_H - 1 }, C_PANEL);
	gfx_hline(c, 0, PANEL_H - 1, D.width, C_ACCENT);
	struct gfx_rect m = menu_button_rect();
	if (D.menu_open) {
		gfx_fill(c, m, C_BUTTON);
	}
	gfx_fill(c, (struct gfx_rect){ 7, 6, 9, 9 }, C_ACCENT); /* the mark */
	gfx_text(c, 22, (PANEL_H - GFX_CELL_H) / 2, "ManiOS", C_LIGHT_TEXT, 1);

	struct window *list[MAX_WINDOWS];
	int n = windows_by_id(list);
	for (int i = 0; i < n; i++) {
		struct gfx_rect b = window_button_rect(i, n);
		bool focused = D.focus == list[i];
		gfx_fill(c, b, focused ? C_ACCENT : C_BUTTON);
		label(c, b.x + 5, b.y + (b.h - GFX_CELL_H) / 2 + 1, b.w - 8, list[i]->title,
		      focused ? C_DARK_TEXT : C_LIGHT_TEXT);
	}
	int cw = gfx_text_width(D.clock, 1);
	gfx_text(c, D.width - cw - 8, (PANEL_H - GFX_CELL_H) / 2, D.clock, C_LIGHT_TEXT, 1);
}

static void draw_menu(struct gfx_canvas *c)
{
	struct gfx_rect r = menu_rect();
	gfx_fill(c, r, C_MENU);
	gfx_outline(c, r, C_FRAME);
	for (int i = 0; i < MENU_COUNT; i++) {
		struct gfx_rect it = menu_item_rect(i);
		if (!MENU[i].label) {
			gfx_hline(c, it.x + 6, it.y + it.h / 2, it.w - 12, C_INACTIVE);
			continue;
		}
		bool selected = i == D.menu_sel;
		if (selected) {
			gfx_fill(c, it, C_ACCENT);
		}
		gfx_text(c, it.x + 10, it.y + (ITEM_H - GFX_CELL_H) / 2 + 1, MENU[i].label,
		         selected ? C_DARK_TEXT : C_LIGHT_TEXT, 1);
	}
}

static void draw_cursor(struct gfx_canvas *c)
{
	for (int y = 0; y < CURSOR_H; y++) {
		for (int x = 0; CURSOR[y][x]; x++) {
			char k = CURSOR[y][x];
			if (k != ' ') {
				gfx_pixel(c, D.px + x, D.py + y, k == 'X' ? GFX_RGB(0, 0, 0) : GFX_RGB(255, 255, 255));
			}
		}
	}
}

void compose(void)
{
	struct gfx_rect r = D.damage;
	if (gfx_rect_empty(r)) {
		return;
	}
	D.damage = (struct gfx_rect){ 0, 0, 0, 0 };
	struct gfx_canvas *c = D.back;
	gfx_set_clip(c, r);
	draw_background(c);
	for (int i = 0; i < D.count; i++) {
		if (!gfx_rect_empty(gfx_intersect(frame_rect(D.stack[i]), r))) {
			draw_window(c, D.stack[i]);
		}
	}
	if (r.y < PANEL_H) {
		draw_panel(c);
	}
	if (D.menu_open) {
		draw_menu(c);
	}
	draw_cursor(c);
	gfx_reset_clip(c);
	gfx_present(&D.screen, c, r);
}

/* HH:MM:SS (UTC) from /dev/time; redraws the clock when it changes. */
void update_clock(void)
{
	char buf[24], text[16];
	lseek(D.time, 0, SEEK_SET);
	long n = D.time >= 0 ? read(D.time, buf, sizeof(buf) - 1) : -1;
	if (n <= 0) {
		return;
	}
	buf[n] = '\0';
	unsigned long t = 0;
	for (char *p = buf; *p >= '0' && *p <= '9'; p++) {
		t = t * 10 + (unsigned long)(*p - '0');
	}
	unsigned long day = t % 86400;
	snprintf(text, sizeof(text), "%02lu:%02lu:%02lu", day / 3600, day / 60 % 60, day % 60);
	if (strcmp(text, D.clock)) {
		strcpy(D.clock, text);
		int cw = gfx_text_width(text, 1);
		damage((struct gfx_rect){ D.width - cw - 8, 0, cw, PANEL_H - 1 });
	}
}
