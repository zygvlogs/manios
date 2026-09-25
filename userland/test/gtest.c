/* libgfx conformance test, run by the kernel's boot self-test (M11). It
 * draws into canvases in memory -- no screen needed -- and checks the
 * exact pixels. Prints only failures and a summary. */
#include <gfx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RED   GFX_RGB(255, 0, 0)
#define BLUE  GFX_RGB(0, 0, 255)
#define BLACK GFX_RGB(0, 0, 0)
#define WHITE GFX_RGB(255, 255, 255)

static int checks, failures;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failures++;
		printf("gtest: FAIL: %s\n", what);
	}
}

static int count(const struct gfx_canvas *c, gfx_color color)
{
	int n = 0;
	for (int i = 0; i < c->width * c->height; i++) {
		n += c->pixels[i] == color;
	}
	return n;
}

static void clear(struct gfx_canvas *c)
{
	gfx_reset_clip(c);
	gfx_fill(c, (struct gfx_rect){ 0, 0, c->width, c->height }, BLACK);
}

static int glyph_bits(char ch)
{
	int n = 0;
	for (int r = 0; r < 9; r++) {
		for (int b = 0; b < 5; b++) {
			n += gfx_font[ch - 0x20][r] >> b & 1;
		}
	}
	return n;
}

static void test_rects(struct gfx_canvas *c)
{
	check(!gfx_canvas_new(0, 5) && !gfx_canvas_new(5000, 10), "impossible canvases are refused");
	check(count(c, BLACK) == c->width * c->height, "a new canvas is opaque black");

	gfx_fill(c, (struct gfx_rect){ -5, -5, 8, 8 }, RED);
	check(count(c, RED) == 9 && gfx_get(c, 2, 2) == RED && gfx_get(c, 3, 3) == BLACK,
	      "a fill hanging off the top-left corner is clipped");
	clear(c);
	gfx_fill(c, (struct gfx_rect){ 18, 17, 10, 10 }, RED);
	check(count(c, RED) == 2 * 3, "a fill hanging off the bottom-right corner is clipped");
	clear(c);
	gfx_set_clip(c, (struct gfx_rect){ 2, 3, 4, 5 });
	gfx_fill(c, (struct gfx_rect){ 0, 0, 20, 20 }, RED);
	check(count(c, RED) == 20 && gfx_get(c, 2, 3) == RED && gfx_get(c, 6, 3) == BLACK,
	      "the clip rectangle limits drawing");
	clear(c);
	gfx_outline(c, (struct gfx_rect){ 1, 1, 5, 4 }, WHITE);
	check(count(c, WHITE) == 5 + 5 + 2 + 2 && gfx_get(c, 3, 2) == BLACK, "an outline");
	clear(c);
	gfx_outline(c, (struct gfx_rect){ 1, 1, 1, 1 }, WHITE);
	check(count(c, WHITE) == 1, "a 1x1 outline is one pixel");

	struct gfx_rect a = { 0, 0, 10, 10 }, b = { 5, 5, 10, 10 };
	struct gfx_rect i = gfx_intersect(a, b);
	check(i.x == 5 && i.y == 5 && i.w == 5 && i.h == 5
	      && gfx_rect_empty(gfx_intersect(a, (struct gfx_rect){ 10, 0, 5, 5 }))
	      && gfx_contains(a, 9, 9) && !gfx_contains(a, 10, 9), "rectangle arithmetic");
}

static void test_lines(struct gfx_canvas *c)
{
	clear(c);
	gfx_line(c, 0, 0, 9, 4, WHITE);
	int ok = count(c, WHITE) == 10 && gfx_get(c, 0, 0) == WHITE && gfx_get(c, 9, 4) == WHITE;
	int last_y = 0;
	for (int x = 0; x <= 9; x++) {
		int hits = 0, y_hit = -1;
		for (int y = 0; y < c->height; y++) {
			if (gfx_get(c, x, y) == WHITE) {
				hits++;
				y_hit = y;
			}
		}
		/* One pixel per column, never more than half a pixel off the line. */
		ok &= hits == 1 && y_hit >= last_y && abs(9 * y_hit - 4 * x) <= 4;
		last_y = y_hit;
	}
	check(ok, "a shallow line has one pixel per column, on the line");
	clear(c);
	gfx_line(c, 3, 1, 3, 8, WHITE);
	gfx_line(c, 8, 2, 1, 2, WHITE);
	check(count(c, WHITE) == 8 + 8 - 1, "vertical and horizontal lines (crossing once)");
	clear(c);
	gfx_line(c, -100, -100, -1, 50, WHITE);
	gfx_line(c, -50, 10, 300, 10, RED);
	check(count(c, WHITE) == 0 && count(c, RED) == c->width, "lines are clipped to the canvas");
}

static void test_blending(struct gfx_canvas *c)
{
	check(gfx_blend(GFX_RGBA(255, 0, 0, 128), BLUE) == GFX_RGB(128, 0, 127),
	      "half-transparent red over blue");
	check(gfx_blend(GFX_RGBA(255, 0, 0, 0), BLUE) == BLUE && gfx_blend(RED, BLUE) == RED,
	      "transparent and opaque colours");
	clear(c);
	gfx_fill(c, (struct gfx_rect){ 0, 0, 4, 4 }, BLUE);
	gfx_fill(c, (struct gfx_rect){ 2, 2, 4, 4 }, GFX_RGBA(255, 255, 255, 0x40));
	check(gfx_get(c, 3, 3) == GFX_RGB(64, 64, 255) && gfx_get(c, 5, 5) == GFX_RGB(64, 64, 64),
	      "a translucent fill blends with what is under it");

	struct gfx_canvas *g = gfx_canvas_new(1, 9);
	gfx_gradient(g, (struct gfx_rect){ 0, 0, 1, 9 }, BLACK, WHITE);
	check(g && gfx_get(g, 0, 0) == BLACK && gfx_get(g, 0, 8) == WHITE && GFX_R(gfx_get(g, 0, 4)) == 127,
	      "a gradient runs from the top colour to the bottom one");
	gfx_canvas_free(g);

	clear(c);
	gfx_fill_circle(c, 10, 10, 0, WHITE);
	check(count(c, WHITE) == 1, "a circle of radius 0 is a pixel");
	gfx_fill_circle(c, 10, 10, 3, WHITE);
	check(gfx_get(c, 7, 10) == WHITE && gfx_get(c, 13, 10) == WHITE && gfx_get(c, 10, 7) == WHITE
	      && gfx_get(c, 10, 13) == WHITE && gfx_get(c, 7, 7) == BLACK && gfx_get(c, 13, 13) == BLACK
	      && count(c, WHITE) == 37, "a circle of radius 3");
}

static void test_blit(struct gfx_canvas *c)
{
	struct gfx_canvas *src = gfx_canvas_new(4, 4);
	for (int i = 0; i < 16; i++) {
		src->pixels[i] = GFX_RGB(i, 0, 0);
	}
	clear(c);
	gfx_blit(c, -2, -2, src, (struct gfx_rect){ 0, 0, 4, 4 });
	check(gfx_get(c, 0, 0) == GFX_RGB(10, 0, 0) && gfx_get(c, 1, 1) == GFX_RGB(15, 0, 0)
	      && gfx_get(c, 2, 2) == BLACK, "a blit is clipped at the destination");
	clear(c);
	gfx_blit(c, 5, 5, src, (struct gfx_rect){ 1, 1, 2, 2 });
	check(gfx_get(c, 5, 5) == GFX_RGB(5, 0, 0) && gfx_get(c, 6, 6) == GFX_RGB(10, 0, 0)
	      && gfx_get(c, 7, 7) == BLACK, "a blit of part of the source");
	src->pixels[0] = GFX_RGBA(255, 255, 255, 0x80);
	gfx_fill(c, (struct gfx_rect){ 0, 0, 1, 1 }, BLUE);
	gfx_blit_blend(c, 0, 0, src, (struct gfx_rect){ 0, 0, 1, 1 });
	check(gfx_get(c, 0, 0) == GFX_RGB(128, 128, 255), "a blending blit");
	gfx_canvas_free(src);

	/* Moving content down inside one canvas (scrolling). */
	clear(c);
	for (int y = 0; y < 5; y++) {
		gfx_hline(c, 0, y, c->width, GFX_RGB(y + 1, 0, 0));
	}
	gfx_blit(c, 0, 1, c, (struct gfx_rect){ 0, 0, c->width, 5 });
	int ok = gfx_get(c, 0, 0) == GFX_RGB(1, 0, 0);
	for (int y = 1; y <= 5; y++) {
		ok &= gfx_get(c, 3, y) == GFX_RGB(y, 0, 0);
	}
	check(ok, "a blit moving rows down within a canvas");
}

static void test_text(struct gfx_canvas *c)
{
	check(gfx_text_width("Hello", 1) == 30 && gfx_text_width("Hello", 2) == 60
	      && gfx_text_width("ab\ncde", 1) == 18 && gfx_text_width("", 1) == 0, "text widths");
	clear(c);
	check(gfx_text(c, 0, 0, "I", WHITE, 1) == GFX_CELL_W && count(c, WHITE) == glyph_bits('I')
	      && gfx_get(c, 1, 1) == WHITE && gfx_get(c, 3, 1) == WHITE && gfx_get(c, 0, 1) == BLACK
	      && gfx_get(c, 2, 0) == BLACK, "a glyph lands in its cell, below a blank row");
	clear(c);
	gfx_text(c, 0, 0, "I", WHITE, 2);
	check(count(c, WHITE) == 4 * glyph_bits('I'), "scale 2 doubles each pixel");
	clear(c);
	gfx_text(c, 0, 0, "\x80", WHITE, 1);
	check(count(c, WHITE) == 20, "a character outside the font draws a box");
	int ok = glyph_bits(' ') == 0;
	for (char ch = '!'; ch <= '~'; ch++) {
		ok &= glyph_bits(ch) > 0;
	}
	check(ok, "every printable character but space has a glyph");
	clear(c);
	gfx_text(c, 0, 0, "y", WHITE, 1);
	int descends = 0;
	for (int x = 0; x < 5; x++) {
		descends |= gfx_get(c, x, 9) == WHITE; /* row 8 of the glyph */
	}
	check(descends, "descenders go below the baseline");
}

int main(void)
{
	struct gfx_canvas *c = gfx_canvas_new(20, 20);
	if (!c) {
		printf("gtest: FAIL: no canvas\n");
		return 1;
	}
	test_rects(c);
	test_lines(c);
	test_blending(c);
	test_blit(c);
	test_text(c);
	gfx_canvas_free(c);
	if (failures) {
		printf("gtest: %d of %d checks failed\n", failures, checks);
		return 1;
	}
	printf("gtest: all %d checks passed\n", checks);
	return 0;
}
