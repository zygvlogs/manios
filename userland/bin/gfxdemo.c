/* gfxdemo [WIDTH HEIGHT | vga] -- switches to a graphics mode (default
 * 640x480; "vga" for 320x200), draws a scene with libgfx, waits for
 * Enter, and returns to text mode.
 *
 * The layout depends only on the screen size, so tests/gfx_test.py can
 * check the screen: eight colour swatches in a row, each `side` pixels
 * square, `side + side / 4` apart, starting at (side / 2, 3 * side). */
#include <gfx.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const gfx_color swatches[8] = {
	GFX_RGB(255, 0, 0),   GFX_RGB(0, 255, 0),   GFX_RGB(0, 0, 255), GFX_RGB(255, 255, 255),
	GFX_RGB(255, 255, 0), GFX_RGB(0, 255, 255), GFX_RGB(255, 0, 255), GFX_RGB(0, 0, 0),
};

static void draw(struct gfx_canvas *c)
{
	int w = c->width, h = c->height, side = w / 16, gap = side + side / 4;
	gfx_gradient(c, (struct gfx_rect){ 0, 0, w, h }, GFX_RGB(0x20, 0x38, 0x70),
	             GFX_RGB(0x08, 0x08, 0x18));
	int scale = w >= 640 ? 2 : 1;
	gfx_text(c, side / 2, side / 2, "ManiOS graphics", GFX_RGB(255, 255, 255), scale);
	for (int i = 0; i < 8; i++) {
		struct gfx_rect r = { side / 2 + i * gap, 3 * side, side, side };
		gfx_fill(c, r, swatches[i]);
		gfx_outline(c, (struct gfx_rect){ r.x - 1, r.y - 1, r.w + 2, r.h + 2 },
		            GFX_RGB(0xC0, 0xC0, 0xC0));
	}

	/* Below the swatches: lines, circles, a translucent panel, a bevel. */
	int top = 5 * side;
	for (int i = 0; i <= 8; i++) {
		gfx_line(c, side / 2, h - side / 2, side / 2 + i * w / 16, top, GFX_RGB(0xFF, 0xC0, 0x40));
	}
	for (int i = 0; i < 4; i++) {
		gfx_fill_circle(c, w / 2 + i * side, top + side, side / 2 - i * 2,
		                GFX_RGBA(0x40 * i, 0xFF, 0x80, 0xC0));
	}
	struct gfx_rect panel = { w / 2 - side, top + 2 * side, w / 2, h - top - 3 * side };
	gfx_fill(c, panel, GFX_RGBA(0xFF, 0xFF, 0xFF, 0x60));
	gfx_bevel(c, panel, true);
	gfx_text(c, panel.x + 6, panel.y + 6, "libgfx: rectangles, lines,\ncircles, blending, text.",
	         GFX_RGB(0, 0, 0), 1);
	gfx_text(c, side / 2, h - GFX_CELL_H - 2, "Press Enter to return to text mode.",
	         GFX_RGB(0xE0, 0xE0, 0xE0), 1);
}

int main(int argc, char **argv)
{
	int w = 640, h = 480;
	if (argc == 2 && !strcmp(argv[1], "vga")) {
		w = h = 0;
	} else if (argc == 3) {
		w = atoi(argv[1]);
		h = atoi(argv[2]);
	} else if (argc != 1) {
		fprintf(stderr, "usage: gfxdemo [WIDTH HEIGHT | vga]\n");
		return 2;
	}
	struct gfx_screen screen;
	if (gfx_screen_open(&screen, w, h) < 0) {
		perror("gfxdemo: /dev/fbctl");
		return 1;
	}
	struct gfx_canvas *c = gfx_canvas_new(screen.width, screen.height);
	if (!c) {
		gfx_screen_close(&screen);
		fprintf(stderr, "gfxdemo: out of memory\n");
		return 1;
	}
	draw(c);
	gfx_present(&screen, c, (struct gfx_rect){ 0, 0, c->width, c->height });
	printf("gfxdemo: %dx%dx%d %s, press Enter\n", screen.width, screen.height, screen.depth,
	       screen.rgb332 ? "rgb332" : "xrgb8888");
	getchar();
	gfx_screen_close(&screen);
	gfx_canvas_free(c);
	printf("gfxdemo: back to text mode\n");
	return 0;
}
