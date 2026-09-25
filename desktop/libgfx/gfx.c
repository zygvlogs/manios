/* Canvases and drawing primitives (gfx.h). */
#include "gfx.h"
#include <stdlib.h>
#include <string.h>

#define MAX_SIDE 4096

struct gfx_canvas *gfx_canvas_new(int width, int height)
{
	if (width <= 0 || height <= 0 || width > MAX_SIDE || height > MAX_SIDE) {
		return NULL;
	}
	struct gfx_canvas *c = malloc(sizeof(*c));
	uint32_t *pixels = malloc((size_t)width * (size_t)height * 4);
	if (!c || !pixels) {
		free(c);
		free(pixels);
		return NULL;
	}
	c->width = width;
	c->height = height;
	c->pixels = pixels;
	gfx_reset_clip(c);
	for (int i = 0; i < width * height; i++) {
		pixels[i] = GFX_RGB(0, 0, 0);
	}
	return c;
}

void gfx_canvas_free(struct gfx_canvas *c)
{
	if (c) {
		free(c->pixels);
		free(c);
	}
}

static int max(int a, int b) { return a > b ? a : b; }
static int min(int a, int b) { return a < b ? a : b; }

struct gfx_rect gfx_intersect(struct gfx_rect a, struct gfx_rect b)
{
	int x0 = max(a.x, b.x), y0 = max(a.y, b.y);
	int x1 = min(a.x + a.w, b.x + b.w), y1 = min(a.y + a.h, b.y + b.h);
	struct gfx_rect r = { x0, y0, x1 - x0, y1 - y0 };
	if (r.w <= 0 || r.h <= 0) {
		r.w = r.h = 0;
	}
	return r;
}

bool gfx_rect_empty(struct gfx_rect r)
{
	return r.w <= 0 || r.h <= 0;
}

bool gfx_contains(struct gfx_rect r, int x, int y)
{
	return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

void gfx_set_clip(struct gfx_canvas *c, struct gfx_rect r)
{
	c->clip = gfx_intersect(r, (struct gfx_rect){ 0, 0, c->width, c->height });
}

void gfx_reset_clip(struct gfx_canvas *c)
{
	c->clip = (struct gfx_rect){ 0, 0, c->width, c->height };
}

/* x / 255, rounded, for x in [0, 255 * 255], without dividing. */
static uint32_t div255(uint32_t x)
{
	x += 128;
	return (x + (x >> 8)) >> 8;
}

gfx_color gfx_blend(gfx_color over, gfx_color under)
{
	uint32_t a = GFX_A(over);
	if (a == 0xFF) {
		return over;
	}
	if (a == 0) {
		return under;
	}
	uint32_t na = 255 - a;
	uint32_t r = div255(GFX_R(over) * a + GFX_R(under) * na);
	uint32_t g = div255(GFX_G(over) * a + GFX_G(under) * na);
	uint32_t b = div255(GFX_B(over) * a + GFX_B(under) * na);
	uint32_t out_a = a + div255(GFX_A(under) * na);
	return out_a << 24 | r << 16 | g << 8 | b;
}

static void put(struct gfx_canvas *c, int x, int y, gfx_color color)
{
	uint32_t *p = &c->pixels[y * c->width + x];
	*p = GFX_A(color) == 0xFF ? color : gfx_blend(color, *p);
}

void gfx_pixel(struct gfx_canvas *c, int x, int y, gfx_color color)
{
	if (gfx_contains(c->clip, x, y)) {
		put(c, x, y, color);
	}
}

gfx_color gfx_get(const struct gfx_canvas *c, int x, int y)
{
	if (x < 0 || y < 0 || x >= c->width || y >= c->height) {
		return 0;
	}
	return c->pixels[y * c->width + x];
}

void gfx_fill(struct gfx_canvas *c, struct gfx_rect r, gfx_color color)
{
	r = gfx_intersect(r, c->clip);
	for (int y = r.y; y < r.y + r.h; y++) {
		uint32_t *row = &c->pixels[y * c->width + r.x];
		if (GFX_A(color) == 0xFF) {
			for (int x = 0; x < r.w; x++) {
				row[x] = color;
			}
		} else {
			for (int x = 0; x < r.w; x++) {
				row[x] = gfx_blend(color, row[x]);
			}
		}
	}
}

void gfx_hline(struct gfx_canvas *c, int x, int y, int w, gfx_color color)
{
	gfx_fill(c, (struct gfx_rect){ x, y, w, 1 }, color);
}

void gfx_vline(struct gfx_canvas *c, int x, int y, int h, gfx_color color)
{
	gfx_fill(c, (struct gfx_rect){ x, y, 1, h }, color);
}

void gfx_outline(struct gfx_canvas *c, struct gfx_rect r, gfx_color color)
{
	if (gfx_rect_empty(r)) {
		return;
	}
	gfx_hline(c, r.x, r.y, r.w, color);
	if (r.h > 1) {
		gfx_hline(c, r.x, r.y + r.h - 1, r.w, color);
	}
	gfx_vline(c, r.x, r.y + 1, r.h - 2, color);
	if (r.w > 1) {
		gfx_vline(c, r.x + r.w - 1, r.y + 1, r.h - 2, color);
	}
}

/* Bresenham's algorithm; each pixel is clipped, so any endpoints work. */
void gfx_line(struct gfx_canvas *c, int x0, int y0, int x1, int y1, gfx_color color)
{
	int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
	int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	for (;;) {
		gfx_pixel(c, x0, y0, color);
		if (x0 == x1 && y0 == y1) {
			return;
		}
		int e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

/* Every pixel whose centre is within the radius: x^2 + y^2 <= r^2 + r
 * gives the usual round shape for small radii. */
void gfx_fill_circle(struct gfx_canvas *c, int cx, int cy, int radius, gfx_color color)
{
	if (radius < 0) {
		return;
	}
	int limit = radius * radius + radius;
	for (int y = -radius; y <= radius; y++) {
		int x = 0;
		while ((x + 1) * (x + 1) + y * y <= limit) {
			x++;
		}
		gfx_hline(c, cx - x, cy + y, 2 * x + 1, color);
	}
}

void gfx_gradient(struct gfx_canvas *c, struct gfx_rect r, gfx_color top, gfx_color bottom)
{
	for (int i = 0; i < r.h; i++) {
		int span = r.h > 1 ? r.h - 1 : 1;
		uint32_t t = (uint32_t)(i * 255 / span), nt = 255 - t;
		gfx_color color = GFX_RGBA(div255(GFX_R(top) * nt + GFX_R(bottom) * t),
		                           div255(GFX_G(top) * nt + GFX_G(bottom) * t),
		                           div255(GFX_B(top) * nt + GFX_B(bottom) * t),
		                           div255(GFX_A(top) * nt + GFX_A(bottom) * t));
		gfx_hline(c, r.x, r.y + i, r.w, color);
	}
}

void gfx_bevel(struct gfx_canvas *c, struct gfx_rect r, bool raised)
{
	gfx_color light = GFX_RGB(0xF4, 0xF4, 0xF0), dark = GFX_RGB(0x50, 0x50, 0x58);
	gfx_color tl = raised ? light : dark, br = raised ? dark : light;
	gfx_hline(c, r.x, r.y, r.w, tl);
	gfx_vline(c, r.x, r.y, r.h, tl);
	gfx_hline(c, r.x, r.y + r.h - 1, r.w, br);
	gfx_vline(c, r.x + r.w - 1, r.y, r.h, br);
}

static void blit(struct gfx_canvas *dst, int x, int y, const struct gfx_canvas *src,
                 struct gfx_rect sr, bool blend)
{
	sr = gfx_intersect(sr, (struct gfx_rect){ 0, 0, src->width, src->height });
	/* Where it lands, clipped; then back to the source rectangle. */
	struct gfx_rect d = gfx_intersect((struct gfx_rect){ x, y, sr.w, sr.h }, dst->clip);
	sr.x += d.x - x;
	sr.y += d.y - y;
	/* Within one canvas, moving down: copy the bottom row first. */
	bool upward = src == dst && d.y > sr.y;
	for (int i = 0; i < d.h; i++) {
		int row = upward ? d.h - 1 - i : i;
		uint32_t *out = &dst->pixels[(d.y + row) * dst->width + d.x];
		const uint32_t *in = &src->pixels[(sr.y + row) * src->width + sr.x];
		if (blend) {
			for (int col = 0; col < d.w; col++) {
				out[col] = gfx_blend(in[col], out[col]);
			}
		} else {
			memmove(out, in, (size_t)d.w * 4);
		}
	}
}

void gfx_blit(struct gfx_canvas *dst, int x, int y, const struct gfx_canvas *src,
              struct gfx_rect src_rect)
{
	blit(dst, x, y, src, src_rect, false);
}

void gfx_blit_blend(struct gfx_canvas *dst, int x, int y, const struct gfx_canvas *src,
                    struct gfx_rect src_rect)
{
	blit(dst, x, y, src, src_rect, true);
}
