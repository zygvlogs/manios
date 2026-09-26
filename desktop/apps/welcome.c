/* welcome -- ManiDE's welcome: its motto and its keys, centred in
 * whatever size its pane has. Closes when asked (Alt+q, the close box)
 * or with Escape or q. */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <string.h>
#include <win.h>

#define BG     GFX_RGB(0x0F, 0x13, 0x19)
#define AMBER  GFX_RGB(0xE0, 0x7A, 0x2E)
#define CYAN   GFX_RGB(0x3F, 0xC8, 0xD8)
#define LIGHT  GFX_RGB(0xD8, 0xDE, 0xE9)
#define DIM    GFX_RGB(0x6B, 0x74, 0x83)

static const char *const WORDS[] = { "Minimalist", "Modular", "Manual" };
static const char *const LINES[] = {
	"Experience the power of simplicity.",
	"Master your environment. Control every byte.",
};
static const char *const KEYS[][2] = {
	{ "Alt+Enter", "terminal" },     { "Alt+d", "run a program" },
	{ "Alt+1..9", "workspaces" },    { "Alt+Shift+1..9", "move a window" },
	{ "Alt+j / k", "next, previous" }, { "Alt+Space", "layout" },
	{ "Alt+q", "close" },            { "F1", "menu" },
};
#define KEYS_N 8
#define KEY_COL (15 * GFX_CELL_W) /* "Alt+Shift+1..9 " */
#define KEY_W (KEY_COL + 15 * GFX_CELL_W)
#define KEY_H (GFX_CELL_H + 4)

/* The largest scale, up to `most`, at which s fits in width. */
static int fit(const char *s, int width, int most)
{
	int scale = most;
	while (scale > 1 && gfx_text_width(s, scale) > width) {
		scale--;
	}
	return scale;
}

static void centred(struct gfx_canvas *c, int y, const char *s, gfx_color color, int scale)
{
	gfx_text(c, (c->width - gfx_text_width(s, scale)) / 2, y, s, color, scale);
}

static void draw(struct win *w)
{
	struct gfx_canvas *c = w->canvas;
	int width = w->width - 16;
	gfx_fill(c, (struct gfx_rect){ 0, 0, w->width, w->height }, BG);

	int big = fit("Welcome to ManiDE", width, 4);
	int mid = fit("Minimalist   Modular   Manual", width, 2);
	int text_h = GFX_CELL_H * big + 14 + GFX_CELL_H * mid + 16 + 2 * (GFX_CELL_H + 3);
	/* The keys in two columns, or one, or not at all: what fits. */
	int key_cols = 2 * KEY_W + 24 <= width ? 2 : KEY_W <= width ? 1 : 0;
	int key_rows = key_cols ? KEYS_N / key_cols : 0;
	if (key_cols && text_h + 18 + key_rows * KEY_H > w->height - 8) {
		key_cols = key_rows = 0;
	}
	int height = text_h + (key_cols ? 18 + key_rows * KEY_H : 0);
	int y = (w->height - height) / 2;
	y = y < 4 ? 4 : y;

	int tx = (c->width - gfx_text_width("Welcome to ManiDE", big)) / 2;
	gfx_text(c, tx, y, "Welcome to ", LIGHT, big);
	gfx_text(c, tx + gfx_text_width("Welcome to ", big), y, "ManiDE", AMBER, big);
	y += GFX_CELL_H * big + 14;

	/* The three words, with dots between them. */
	int gap = 3 * GFX_CELL_W * mid, total = -gap;
	for (int i = 0; i < 3; i++) {
		total += gfx_text_width(WORDS[i], mid) + gap;
	}
	int x = (c->width - total) / 2;
	for (int i = 0; i < 3; i++) {
		x += gfx_text(c, x, y, WORDS[i], CYAN, mid);
		if (i < 2) {
			gfx_fill_circle(c, x + gap / 2, y + GFX_CELL_H * mid / 2, mid + 1, AMBER);
			x += gap;
		}
	}
	y += GFX_CELL_H * mid + 16;
	for (int i = 0; i < 2; i++) {
		centred(c, y, LINES[i], DIM, 1);
		y += GFX_CELL_H + 3;
	}
	if (key_cols) {
		y += 18;
		int all = key_cols * KEY_W + (key_cols - 1) * 24, left = (c->width - all) / 2;
		for (int i = 0; i < KEYS_N; i++) {
			int kx = left + (i % key_cols) * (KEY_W + 24), ky = y + (i / key_cols) * KEY_H;
			gfx_text(c, kx, ky, KEYS[i][0], AMBER, 1);
			gfx_text(c, kx + KEY_COL, ky, KEYS[i][1], LIGHT, 1);
		}
	}
	win_flush(w, 0, w->height);
}

int main(void)
{
	struct win *w = win_open("Welcome", 420, 240);
	if (!w) {
		fprintf(stderr, "welcome: no window: %s\n", strerror(errno));
		return 1;
	}
	draw(w);
	for (;;) {
		struct win_event e;
		int got = win_next(w, &e, -1);
		if (got < 0 || e.type == WIN_CLOSE
		    || (e.type == WIN_KEY && !e.alt && (e.key == 0x1B || e.key == 'q'))) {
			break;
		}
		if (e.type == WIN_RESIZE) {
			draw(w);
		}
	}
	win_close(w);
	return 0;
}
