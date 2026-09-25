/* libgfx: 2D graphics for ManiOS programs (M11). Design notes:
 * docs/milestones/M11-graphics.md.
 *
 * Programs draw into a canvas in memory -- 32-bit 0xAARRGGBB pixels --
 * and present rectangles of it to the screen (/dev/fb), which converts
 * to whatever the display uses. Everything is clipped to the canvas and
 * its clip rectangle, and uses integer arithmetic only: a 386 may have
 * no floating-point unit. */
#ifndef MANIOS_GFX_H
#define MANIOS_GFX_H

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t gfx_color; /* 0xAARRGGBB; alpha 0xFF is opaque */
#define GFX_RGB(r, g, b) (0xFF000000u | (uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))
#define GFX_RGBA(r, g, b, a) ((uint32_t)(a) << 24 | (uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))
#define GFX_R(c) ((c) >> 16 & 0xFF)
#define GFX_G(c) ((c) >> 8 & 0xFF)
#define GFX_B(c) ((c) & 0xFF)
#define GFX_A(c) ((c) >> 24)

struct gfx_rect {
	int x, y, w, h;
};

struct gfx_canvas {
	int width, height;
	uint32_t *pixels; /* row-major, `width` pixels per row */
	struct gfx_rect clip;
};

/* A canvas of opaque black; NULL if out of memory or too large. */
struct gfx_canvas *gfx_canvas_new(int width, int height);
void gfx_canvas_free(struct gfx_canvas *c);

/* The intersection of two rectangles (empty: w or h <= 0). */
struct gfx_rect gfx_intersect(struct gfx_rect a, struct gfx_rect b);
bool gfx_rect_empty(struct gfx_rect r);
bool gfx_contains(struct gfx_rect r, int x, int y);

/* Drawing is limited to the clip rectangle (initially the canvas). */
void gfx_set_clip(struct gfx_canvas *c, struct gfx_rect r);
void gfx_reset_clip(struct gfx_canvas *c);

/* A colour with alpha < 0xFF is blended over what is there ("source
 * over"); an opaque one replaces it. */
void gfx_pixel(struct gfx_canvas *c, int x, int y, gfx_color color);
gfx_color gfx_get(const struct gfx_canvas *c, int x, int y); /* 0 outside */
void gfx_fill(struct gfx_canvas *c, struct gfx_rect r, gfx_color color);
void gfx_hline(struct gfx_canvas *c, int x, int y, int w, gfx_color color);
void gfx_vline(struct gfx_canvas *c, int x, int y, int h, gfx_color color);
void gfx_outline(struct gfx_canvas *c, struct gfx_rect r, gfx_color color);
void gfx_line(struct gfx_canvas *c, int x0, int y0, int x1, int y1, gfx_color color);
void gfx_fill_circle(struct gfx_canvas *c, int cx, int cy, int radius, gfx_color color);
/* Top to bottom, from `top` to `bottom`, row by row. */
void gfx_gradient(struct gfx_canvas *c, struct gfx_rect r, gfx_color top, gfx_color bottom);
/* A 3D border: raised (light top-left) or sunken. */
void gfx_bevel(struct gfx_canvas *c, struct gfx_rect r, bool raised);

/* Copies `src_rect` of src to (x, y) of dst, replacing (gfx_blit) or
 * blending by each source pixel's alpha (gfx_blit_blend). */
void gfx_blit(struct gfx_canvas *dst, int x, int y, const struct gfx_canvas *src,
              struct gfx_rect src_rect);
void gfx_blit_blend(struct gfx_canvas *dst, int x, int y, const struct gfx_canvas *src,
                    struct gfx_rect src_rect);

/* Blends one colour over another: the "source over" operator. */
gfx_color gfx_blend(gfx_color over, gfx_color under);

/* Text in the ManiOS font (font.txt): cells of 6x11 pixels, times
 * `scale`. (x, y) is the top-left of the first cell. '\n' starts a new
 * line; characters outside printable ASCII draw as a box. Returns the
 * width of the widest line drawn. */
#define GFX_CELL_W 6
#define GFX_CELL_H 11
int gfx_text(struct gfx_canvas *c, int x, int y, const char *s, gfx_color color, int scale);
int gfx_text_width(const char *s, int scale);
extern const uint8_t gfx_font[95][9];

/* The screen: /dev/fb in a graphics mode. */
struct gfx_screen {
	int fb, ctl;
	int width, height, pitch, depth;
	bool rgb332; /* VGA: one byte per pixel, RGB 3-3-2 */
};
/* Switches to a width x height mode (Bochs VBE), or VGA's 320x200 when
 * width is 0. Returns 0, or -1 with errno set. */
int gfx_screen_open(struct gfx_screen *s, int width, int height);
/* Back to text mode. */
void gfx_screen_close(struct gfx_screen *s);
/* Copies rectangle r of the canvas (the screen's size) to the screen. */
void gfx_present(struct gfx_screen *s, const struct gfx_canvas *c, struct gfx_rect r);

#endif
