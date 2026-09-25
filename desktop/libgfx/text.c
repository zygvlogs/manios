/* Text in the ManiOS font (font.txt, font_data.c). */
#include "gfx.h"

/* The box drawn for characters the font doesn't have. */
static const uint8_t missing[9] = { 0x1f, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1f, 0x00, 0x00 };

static void glyph(struct gfx_canvas *c, int x, int y, unsigned char ch, gfx_color color, int scale)
{
	const uint8_t *rows = ch >= 0x20 && ch < 0x7F ? gfx_font[ch - 0x20] : missing;
	for (int r = 0; r < 9; r++) {
		for (int b = 0; b < 5; b++) {
			if (rows[r] >> (4 - b) & 1) {
				gfx_fill(c, (struct gfx_rect){ x + b * scale, y + (r + 1) * scale, scale, scale },
				         color);
			}
		}
	}
}

int gfx_text(struct gfx_canvas *c, int x, int y, const char *s, gfx_color color, int scale)
{
	if (scale < 1) {
		scale = 1;
	}
	int cx = x, widest = 0;
	for (; *s; s++) {
		if (*s == '\n') {
			cx = x;
			y += GFX_CELL_H * scale;
			continue;
		}
		glyph(c, cx, y, (unsigned char)*s, color, scale);
		cx += GFX_CELL_W * scale;
		if (cx - x > widest) {
			widest = cx - x;
		}
	}
	return widest;
}

int gfx_text_width(const char *s, int scale)
{
	if (scale < 1) {
		scale = 1;
	}
	int line = 0, widest = 0;
	for (; *s; s++) {
		line = *s == '\n' ? 0 : line + 1;
		if (line > widest) {
			widest = line;
		}
	}
	return widest * GFX_CELL_W * scale;
}
