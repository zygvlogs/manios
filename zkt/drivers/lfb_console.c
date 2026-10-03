#include "lfb_console.h"
#include <stdbool.h>
#include <stdint.h>
#include "cpu.h"
#include "font8x16_basic.h"
#include "kstring.h"

#define TAB_WIDTH 8
#define DEFAULT_FG 7
#define DEFAULT_BG 0

static volatile uint8_t *lfb_base;
static uint32_t lfb_width, lfb_height, lfb_pitch;
static size_t cols, rows;
static size_t cursor_row, cursor_col;
static uint8_t fg_color, bg_color;

static const uint32_t VGA_RGB[16] = {
	0x000000, /* 0 black */
	0x0000AA, /* 1 blue */
	0x00AA00, /* 2 green */
	0x00AAAA, /* 3 cyan */
	0xAA0000, /* 4 red */
	0xAA00AA, /* 5 magenta */
	0xAA5500, /* 6 brown */
	0xAAAAAA, /* 7 light grey */
	0x555555, /* 8 dark grey */
	0x5555FF, /* 9 light blue */
	0x55FF55, /* 10 light green */
	0x55FFFF, /* 11 light cyan */
	0xFF5555, /* 12 light red */
	0xFF55FF, /* 13 light magenta */
	0xFFFF55, /* 14 yellow */
	0xFFFFFF, /* 15 white */
};

static const uint8_t ANSI_TO_VGA[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

/* ANSI escape state. */
#define CSI_PARAMS 8
static enum { TEXT, ESCAPE, CSI } esc_state;
static unsigned esc_params[CSI_PARAMS], esc_count;
static bool esc_private;

static inline uint32_t *pixel_ptr(size_t x, size_t y)
{
	return (uint32_t *)(lfb_base + y * lfb_pitch + x * 4);
}

static void draw_glyph(char c, size_t col, size_t row)
{
	uint32_t fg = VGA_RGB[fg_color & 0x0F];
	uint32_t bg = VGA_RGB[bg_color & 0x0F];
	const uint8_t *glyph;
	if ((unsigned char)c >= FONT_FIRST && (unsigned char)c <= FONT_LAST) {
		glyph = font8x16_basic[(unsigned char)c - FONT_FIRST];
	} else {
		static const uint8_t block[FONT_HEIGHT] = {
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		};
		glyph = block;
	}

	size_t px = col * FONT_WIDTH;
	size_t py = row * FONT_HEIGHT;
	for (size_t dy = 0; dy < FONT_HEIGHT; dy++) {
		uint8_t bits = glyph[dy];
		uint32_t *p = pixel_ptr(px, py + dy);
		for (size_t dx = 0; dx < FONT_WIDTH; dx++) {
			p[dx] = (bits & (0x80 >> dx)) ? fg : bg;
		}
	}
}

static void erase(size_t from, size_t to)
{
	for (size_t i = from; i < to; i++) {
		draw_glyph(' ', i % cols, i / cols);
	}
}

static void scroll(void)
{
	uint8_t *dst = (uint8_t *)lfb_base;
	uint8_t *src = (uint8_t *)lfb_base + FONT_HEIGHT * lfb_pitch;
	size_t move_bytes = (rows - 1) * FONT_HEIGHT * lfb_pitch;
	for (size_t i = 0; i < move_bytes; i++) {
		dst[i] = src[i];
	}
	uint32_t bg = VGA_RGB[bg_color & 0x0F];
	for (size_t y = (rows - 1) * FONT_HEIGHT; y < rows * FONT_HEIGHT; y++) {
		uint32_t *p = pixel_ptr(0, y);
		for (size_t x = 0; x < lfb_width; x++) {
			p[x] = bg;
		}
	}
}

static void newline(void)
{
	cursor_col = 0;
	if (++cursor_row == rows) {
		scroll();
		cursor_row = rows - 1;
	}
}

static void select_graphic_rendition(void)
{
	if (esc_count == 0) {
		esc_params[esc_count++] = 0;
	}
	for (unsigned i = 0; i < esc_count; i++) {
		unsigned p = esc_params[i];
		if (p == 0) {
			fg_color = DEFAULT_FG;
			bg_color = DEFAULT_BG;
		} else if (p == 1) {
			fg_color |= 0x08;
		} else if (p == 22) {
			fg_color &= (uint8_t)~0x08;
		} else if (p >= 30 && p <= 37) {
			fg_color = (uint8_t)((fg_color & 0xF8) | ANSI_TO_VGA[p - 30]);
		} else if (p == 39) {
			fg_color = (uint8_t)((fg_color & 0xF8) | (DEFAULT_FG & 0x07));
		} else if (p >= 90 && p <= 97) {
			fg_color = (uint8_t)((fg_color & 0xF0) | 0x08 | ANSI_TO_VGA[p - 90]);
		} else if ((p >= 40 && p <= 47) || (p >= 100 && p <= 107)) {
			bg_color = (uint8_t)((bg_color & 0x0F) | ANSI_TO_VGA[p % 10] << 4);
		} else if (p == 49) {
			bg_color &= 0x0F;
		}
	}
}

static unsigned param(unsigned i, unsigned fallback)
{
	return i < esc_count && esc_params[i] ? esc_params[i] : fallback;
}

static void control_sequence(char final)
{
	size_t here = cursor_row * cols + cursor_col;
	unsigned n = param(0, 1);
	switch (final) {
	case 'm':
		select_graphic_rendition();
		break;
	case 'H':
	case 'f':
		cursor_row = param(0, 1) > rows ? rows - 1 : param(0, 1) - 1;
		cursor_col = param(1, 1) > cols ? cols - 1 : param(1, 1) - 1;
		break;
	case 'A':
		cursor_row = n > cursor_row ? 0 : cursor_row - n;
		break;
	case 'B':
		cursor_row = cursor_row + n >= rows ? rows - 1 : cursor_row + n;
		break;
	case 'C':
		cursor_col = cursor_col + n >= cols ? cols - 1 : cursor_col + n;
		break;
	case 'D':
		cursor_col = n > cursor_col ? 0 : cursor_col - n;
		break;
	case 'J':
		erase(param(0, 0) == 0 ? here : 0,
		      param(0, 0) == 1 ? here + 1 : rows * cols);
		break;
	case 'K':
		erase(param(0, 0) == 0 ? here : cursor_row * cols,
		      param(0, 0) == 1 ? here + 1 : (cursor_row + 1) * cols);
		break;
	}
}

static bool escape(char c)
{
	if (esc_state == TEXT) {
		if (c != '\033') {
			return false;
		}
		esc_state = ESCAPE;
		return true;
	}
	if (esc_state == ESCAPE) {
		esc_state = c == '[' ? CSI : TEXT;
		esc_count = 0;
		esc_private = false;
		return true;
	}
	if (c >= '0' && c <= '9') {
		if (esc_count == 0) {
			esc_params[esc_count++] = 0;
		}
		unsigned *p = &esc_params[esc_count - 1];
		*p = *p < 10000 ? *p * 10 + (unsigned)(c - '0') : *p;
	} else if (c == ';') {
		if (esc_count == 0) {
			esc_params[esc_count++] = 0;
		}
		if (esc_count < CSI_PARAMS) {
			esc_params[esc_count++] = 0;
		}
	} else if (c == '?') {
		esc_private = true;
	} else if (c >= 0x40 && c <= 0x7E) {
		if (!esc_private) {
			control_sequence(c);
		}
		esc_state = TEXT;
	} else if ((unsigned char)c < 0x20 || (unsigned char)c > 0x3F) {
		esc_state = TEXT;
	}
	return true;
}

static void lfb_putc(char c)
{
	if (escape(c)) {
		return;
	}
	switch (c) {
	case '\n':
		newline();
		return;
	case '\r':
		cursor_col = 0;
		return;
	case '\b':
		if (cursor_col > 0) {
			cursor_col--;
		}
		return;
	case '\t':
		do {
			lfb_putc(' ');
		} while (cursor_col % TAB_WIDTH);
		return;
	}

	draw_glyph(c, cursor_col, cursor_row);
	if (++cursor_col == cols) {
		newline();
	}
}

void lfb_console_init(uintptr_t base, uint32_t width, uint32_t height,
                      uint32_t pitch)
{
	lfb_base = (volatile uint8_t *)base;
	lfb_width = width;
	lfb_height = height;
	lfb_pitch = pitch;
	cols = width / FONT_WIDTH;
	rows = height / FONT_HEIGHT;
	cursor_row = 0;
	cursor_col = 0;
	fg_color = DEFAULT_FG;
	bg_color = DEFAULT_BG;
	esc_state = TEXT;

	uint32_t bg = VGA_RGB[bg_color];
	for (uint32_t y = 0; y < height; y++) {
		uint32_t *p = (uint32_t *)(lfb_base + y * pitch);
		for (uint32_t x = 0; x < width; x++) {
			p[x] = bg;
		}
	}
}

void lfb_console_write(const char *buf, size_t len)
{
	uint32_t flags = cpu_irq_save();
	for (size_t i = 0; i < len; i++) {
		lfb_putc(buf[i]);
	}
	cpu_irq_restore(flags);
}
