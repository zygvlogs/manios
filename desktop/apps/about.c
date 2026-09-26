/* about -- what this system is, centred in whatever size its pane has.
 * Closes when asked (Alt+q, the close box) or from any key. */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <string.h>
#include <win.h>

#define WIDTH 340
#define HEIGHT 170
#define BG     GFX_RGB(0x1E, 0x24, 0x30)
#define AMBER  GFX_RGB(0xE0, 0x7A, 0x2E)
#define LIGHT  GFX_RGB(0xE8, 0xE8, 0xE8)
#define DIM    GFX_RGB(0x9A, 0xA2, 0xB0)

static const char *const LINES[] = {
	"An independent operating system for i386 PCs,",
	"with per-process namespaces in the spirit of",
	"Plan 9: devices, windows and other machines",
	"are all files.",
};

static void draw(struct win *w)
{
	struct gfx_canvas *c = w->canvas;
	char up[48];
	uint32_t s = uptime_ms() / 1000;
	gfx_fill(c, (struct gfx_rect){ 0, 0, w->width, w->height }, BG);
	/* The WIDTH x HEIGHT card, in the middle (or the top-left, if the
	 * window is smaller). */
	int x = w->width > WIDTH ? (w->width - WIDTH) / 2 : 0;
	int y = w->height > HEIGHT ? (w->height - HEIGHT) / 2 : 0;
	gfx_fill(c, (struct gfx_rect){ x + 16, y + 16, 22, 22 }, AMBER);
	gfx_text(c, x + 48, y + 16, "ManiOS", LIGHT, 2);
	gfx_text(c, x + 48 + gfx_text_width("ManiOS", 2) + 10, y + 27, "version " MANIOS_VERSION, DIM, 1);
	gfx_text(c, x + 16, y + 48, "Kernel: ZKT (ZygKernel Technology)", AMBER, 1);
	for (int i = 0; i < 4; i++) {
		gfx_text(c, x + 16, y + 68 + i * GFX_CELL_H, LINES[i], LIGHT, 1);
	}
	snprintf(up, sizeof(up), "Up %lu:%02lu:%02lu", (unsigned long)(s / 3600),
	         (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
	gfx_text(c, x + 16, y + 124, up, DIM, 1);
	gfx_text(c, x + 16, y + 146, "Press any key to close.", DIM, 1);
	win_flush(w, 0, w->height);
}

int main(void)
{
	struct win *w = win_open("About ManiOS", WIDTH, HEIGHT);
	if (!w) {
		fprintf(stderr, "about: no window: %s\n", strerror(errno));
		return 1;
	}
	for (;;) {
		draw(w);
		struct win_event e;
		int got = win_next(w, &e, 1000);
		if (got < 0 || (got && (e.type == WIN_CLOSE || (e.type == WIN_KEY && !e.alt)))) {
			break;
		}
	}
	win_close(w);
	return 0;
}
