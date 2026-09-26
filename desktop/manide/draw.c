/* Composition (DESIGN.md §3, §6). Everything is drawn into the back
 * buffer, limited to the damaged rectangle, bottom layer first, and that
 * rectangle is then copied to the screen. */
#include "manide.h"
#include <manios.h>
#include <stdio.h>
#include <string.h>

#define MENU_W 170
#define ITEM_H 20
#define SEPARATOR_H 7
#define TEXT_Y ((BAR_H - GFX_CELL_H) / 2 + 1)
/* The bar, left to right: the name, the workspaces, the layout, facts. */
#define LOGO_X 6
#define WS_X 84
#define WS_STEP 14
#define LAYOUT_X (WS_X + WORKSPACES * WS_STEP + 2)
#define FACTS_X (LAYOUT_X + 9 * GFX_CELL_W) /* after "[grid] | " */

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

void damage_bar(void)
{
	damage((struct gfx_rect){ 0, 0, D.width, BAR_H });
}

/* --- geometry --- */

struct gfx_rect cursor_rect(void)
{
	return (struct gfx_rect){ D.px, D.py, CURSOR_W, CURSOR_H };
}

struct gfx_rect bar_logo_rect(void)
{
	return (struct gfx_rect){ 0, 0, LOGO_X + gfx_text_width("ManiDE", 1) + 6, BAR_H };
}

struct gfx_rect bar_ws_rect(int ws)
{
	return (struct gfx_rect){ WS_X + ws * WS_STEP, 2, WS_STEP - 2, BAR_H - 4 };
}

struct gfx_rect menu_item_rect(int i)
{
	int y = BAR_H + 2;
	for (int k = 0; k < i; k++) {
		y += MENU[k].label ? ITEM_H : SEPARATOR_H;
	}
	return (struct gfx_rect){ 1, y, MENU_W - 2, MENU[i].label ? ITEM_H : SEPARATOR_H };
}

struct gfx_rect menu_rect(void)
{
	struct gfx_rect last = menu_item_rect(MENU_COUNT - 1);
	return (struct gfx_rect){ 0, BAR_H, MENU_W, last.y + last.h + 2 - BAR_H };
}

/* --- drawing --- */

/* Text cut to fit `width` pixels; returns where it ended. */
static int label(struct gfx_canvas *c, int x, int y, int width, const char *s, gfx_color color)
{
	char buf[192];
	int fit = width / GFX_CELL_W;
	if (fit <= 0) {
		return x;
	}
	strlcpy(buf, s, sizeof(buf));
	if ((int)strlen(buf) > fit) {
		buf[fit] = '\0';
	}
	gfx_text(c, x, y, buf, color, 1);
	return x + gfx_text_width(buf, 1);
}

/* The facts, their " | " separators dimmer than the rest. */
static void facts(struct gfx_canvas *c, int x, int width, const char *s)
{
	int end = x + width;
	while (*s && x < end) {
		const char *sep = strstr(s, " | ");
		char part[96];
		size_t n = sep ? (size_t)(sep - s) : strlen(s);
		if (n >= sizeof(part)) {
			n = sizeof(part) - 1;
		}
		memcpy(part, s, n);
		part[n] = '\0';
		x = label(c, x, TEXT_Y, end - x, part, C_TEXT);
		if (!sep) {
			break;
		}
		x = label(c, x, TEXT_Y, end - x, " | ", C_DIM);
		s = sep + 3;
	}
}

static void draw_bar(struct gfx_canvas *c)
{
	gfx_fill(c, (struct gfx_rect){ 0, 0, D.width, BAR_H - 1 }, C_BAR);
	gfx_hline(c, 0, BAR_H - 1, D.width, C_EDGE);
	if (D.menu_open) {
		gfx_fill(c, bar_logo_rect(), C_HEAD);
	}
	int x = gfx_text(c, LOGO_X, TEXT_Y, "ManiDE", C_ACCENT, 1) + LOGO_X;
	gfx_text(c, x, TEXT_Y, " | ws:", C_DIM, 1);
	for (int i = 0; i < WORKSPACES; i++) {
		bool used = false;
		for (int k = 0; k < D.count; k++) {
			used |= D.order[k]->ws == i;
		}
		struct gfx_rect r = bar_ws_rect(i);
		char digit[2] = { (char)('1' + i), '\0' };
		if (i == D.ws) {
			gfx_fill(c, r, C_ACCENT);
		}
		gfx_text(c, r.x + (r.w - GFX_CELL_W) / 2, TEXT_Y, digit,
		         i == D.ws ? C_DARK : used ? C_TEXT : C_DIM, 1);
	}
	char layout[16];
	snprintf(layout, sizeof(layout), "[%s]", layout_name(D.wss[D.ws].layout));
	gfx_text(c, LAYOUT_X, TEXT_Y, layout, C_DIM, 1);

	const char *right = bar_right();
	int rx = D.width - 6 - gfx_text_width(right, 1);
	gfx_text(c, rx, TEXT_Y, right, C_TEXT, 1);
	int fx = FACTS_X, fw = rx - 12 - FACTS_X;
	if (D.prompt_open) {
		char line[PROMPT_MAX + 8];
		snprintf(line, sizeof(line), "run: %s", D.prompt);
		int end = label(c, fx, TEXT_Y, fw, line, C_ACCENT);
		gfx_fill(c, (struct gfx_rect){ end, TEXT_Y, 2, GFX_CELL_H - 1 }, C_ACCENT);
		label(c, end + 8, TEXT_Y, fx + fw - end - 8, D.hint, C_DIM);
	} else {
		gfx_text(c, fx - 3 * GFX_CELL_W, TEXT_Y, " | ", C_DIM, 1);
		facts(c, fx, fw, bar_left());
	}
}

/* An empty workspace: what to do with it. */
static void draw_empty(struct gfx_canvas *c)
{
	struct gfx_rect a = tiling_area();
	static const char *const HINTS[] = {
		"Alt+Enter  terminal       Alt+d      run a program",
		"Alt+1..9   workspaces     Alt+Space  layout",
		"Alt+j/k    focus          Alt+q      close",
		"F1         menu           Alt+Q      exit ManiDE",
	};
	const char *name = "ManiDE";
	int scale = D.width >= 800 ? 6 : 4;
	int y = a.y + a.h / 2 - GFX_CELL_H * scale;
	gfx_text(c, a.x + (a.w - gfx_text_width(name, scale)) / 2, y, name, GFX_RGBA(255, 255, 255, 24),
	         scale);
	y += GFX_CELL_H * scale + 16;
	for (unsigned i = 0; i < sizeof(HINTS) / sizeof(HINTS[0]); i++) {
		gfx_text(c, a.x + (a.w - gfx_text_width(HINTS[i], 1)) / 2, y, HINTS[i], C_DIM, 1);
		y += GFX_CELL_H + 4;
	}
}

static void draw_pane(struct gfx_canvas *c, struct window *w)
{
	bool focused = D.focus == w;
	struct gfx_rect s = strip_rect(w), x = close_rect(w), in = w->content;
	gfx_outline(c, w->pane, focused ? C_FOCUS : C_EDGE);
	gfx_fill(c, s, C_HEAD);
	label(c, s.x + 5, s.y + (s.h - GFX_CELL_H) / 2 + 1, x.x - s.x - 8, w->title,
	      focused ? C_FOCUS : C_DIM);
	int cx = x.x + 3, cy = s.y + (s.h - 7) / 2;
	gfx_line(c, cx, cy, cx + 6, cy + 6, focused ? C_TEXT : C_DIM);
	gfx_line(c, cx + 6, cy, cx, cy + 6, focused ? C_TEXT : C_DIM);

	/* The image at the content's top-left; around it, if it is smaller
	 * (it hasn't taken the size it was offered, or can't), the pane. */
	int iw = w->image->width < in.w ? w->image->width : in.w;
	int ih = w->image->height < in.h ? w->image->height : in.h;
	gfx_blit(c, in.x, in.y, w->image, (struct gfx_rect){ 0, 0, iw, ih });
	gfx_fill(c, (struct gfx_rect){ in.x + iw, in.y, in.w - iw, in.h }, C_PANE);
	gfx_fill(c, (struct gfx_rect){ in.x, in.y + ih, iw, in.h - ih }, C_PANE);
}

static void draw_menu(struct gfx_canvas *c)
{
	struct gfx_rect r = menu_rect();
	gfx_fill(c, r, C_MENU);
	gfx_outline(c, r, C_EDGE);
	for (int i = 0; i < MENU_COUNT; i++) {
		struct gfx_rect it = menu_item_rect(i);
		if (!MENU[i].label) {
			gfx_hline(c, it.x + 6, it.y + it.h / 2, it.w - 12, C_EDGE);
			continue;
		}
		bool selected = i == D.menu_sel;
		if (selected) {
			gfx_fill(c, it, C_FOCUS);
		}
		gfx_text(c, it.x + 10, it.y + (ITEM_H - GFX_CELL_H) / 2 + 1, MENU[i].label,
		         selected ? C_DARK : C_TEXT, 1);
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
	gfx_fill(c, r, C_DESK);
	bool any = false;
	for (int i = 0; i < D.count; i++) {
		struct window *w = D.order[i];
		if (w->shown) {
			any = true;
			if (!gfx_rect_empty(gfx_intersect(w->pane, r))) {
				draw_pane(c, w);
			}
		}
	}
	if (!any) {
		draw_empty(c);
	}
	if (r.y < BAR_H) {
		draw_bar(c);
	}
	if (D.menu_open) {
		draw_menu(c);
	}
	draw_cursor(c);
	gfx_reset_clip(c);
	gfx_present(&D.screen, c, r);
}
