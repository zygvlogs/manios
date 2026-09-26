#include "vga_text.h"
#include <stdbool.h>
#include <stdint.h>
#include "cpu.h"
#include "device.h"
#include "io.h"
#include "memlayout.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_DEFAULT_COLOR 0x07 /* light grey on black */
#define VGA_TEXT_PHYS 0xB8000
#define TAB_WIDTH 8

#define CRTC_INDEX 0x3D4
#define CRTC_DATA  0x3D5
#define CRTC_CURSOR_HIGH 0x0E
#define CRTC_CURSOR_LOW  0x0F

static uint16_t *const vga_hardware = P2V(VGA_TEXT_PHYS);
/* While a graphics mode owns the display, text goes to a shadow copy,
 * shown again when text mode returns. */
static uint16_t shadow[VGA_WIDTH * VGA_HEIGHT];
static uint16_t *vga_memory = P2V(VGA_TEXT_PHYS);
static size_t vga_row = 0;
static size_t vga_col = 0;
static uint8_t vga_color = VGA_DEFAULT_COLOR;

/* A subset of ANSI (ECMA-48) escape sequences, so programs that colour
 * their output (fetch, top) read well here too: SGR colours, cursor
 * position and movement, erasing the screen or a line. Others are
 * swallowed. */
#define CSI_PARAMS 8
static enum { TEXT, ESCAPE, CSI } esc_state;
static unsigned esc_params[CSI_PARAMS], esc_count;
static bool esc_private; /* "ESC [ ?": DEC modes, ignored */

/* ANSI's colour order (black red green yellow blue magenta cyan white)
 * in the VGA palette's. */
static const uint8_t ANSI_TO_VGA[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

static inline uint16_t vga_entry(char c, uint8_t color)
{
	return (uint16_t)(uint8_t)c | (uint16_t)color << 8;
}

static void update_cursor(void)
{
	if (vga_memory == shadow) {
		return;
	}
	uint16_t pos = (uint16_t)(vga_row * VGA_WIDTH + vga_col);
	outb(CRTC_INDEX, CRTC_CURSOR_LOW);
	outb(CRTC_DATA, (uint8_t)(pos & 0xFF));
	outb(CRTC_INDEX, CRTC_CURSOR_HIGH);
	outb(CRTC_DATA, (uint8_t)(pos >> 8));
}

static void erase(size_t from, size_t to)
{
	for (size_t i = from; i < to; i++) {
		vga_memory[i] = vga_entry(' ', VGA_DEFAULT_COLOR);
	}
}

void vga_clear(void)
{
	erase(0, VGA_WIDTH * VGA_HEIGHT);
	vga_row = 0;
	vga_col = 0;
	vga_color = VGA_DEFAULT_COLOR;
	esc_state = TEXT;
	update_cursor();
}

static void vga_scroll(void)
{
	for (size_t i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++) {
		vga_memory[i] = vga_memory[i + VGA_WIDTH];
	}
	for (size_t x = 0; x < VGA_WIDTH; x++) {
		vga_memory[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', VGA_DEFAULT_COLOR);
	}
	vga_row = VGA_HEIGHT - 1;
}

static void newline(void)
{
	vga_col = 0;
	if (++vga_row == VGA_HEIGHT) {
		vga_scroll();
	}
}

static void select_graphic_rendition(void)
{
	if (esc_count == 0) {
		esc_params[esc_count++] = 0; /* ESC [ m: reset */
	}
	for (unsigned i = 0; i < esc_count; i++) {
		unsigned p = esc_params[i];
		if (p == 0) {
			vga_color = VGA_DEFAULT_COLOR;
		} else if (p == 1) {
			vga_color |= 0x08; /* bold: the bright colours */
		} else if (p == 22) {
			vga_color &= (uint8_t)~0x08;
		} else if (p >= 30 && p <= 37) {
			vga_color = (uint8_t)((vga_color & 0xF8) | ANSI_TO_VGA[p - 30]);
		} else if (p == 39) {
			vga_color = (uint8_t)((vga_color & 0xF8) | (VGA_DEFAULT_COLOR & 0x07));
		} else if (p >= 90 && p <= 97) {
			vga_color = (uint8_t)((vga_color & 0xF0) | 0x08 | ANSI_TO_VGA[p - 90]);
		} else if ((p >= 40 && p <= 47) || (p >= 100 && p <= 107)) {
			/* Bit 7 blinks rather than brightens: bright backgrounds
			 * come out plain. */
			vga_color = (uint8_t)((vga_color & 0x0F) | ANSI_TO_VGA[p % 10] << 4);
		} else if (p == 49) {
			vga_color &= 0x0F;
		}
	}
}

static unsigned param(unsigned i, unsigned fallback)
{
	return i < esc_count && esc_params[i] ? esc_params[i] : fallback;
}

static void control_sequence(char final)
{
	size_t here = vga_row * VGA_WIDTH + vga_col;
	unsigned n = param(0, 1);
	switch (final) {
	case 'm':
		select_graphic_rendition();
		break;
	case 'H':
	case 'f':
		vga_row = param(0, 1) > VGA_HEIGHT ? VGA_HEIGHT - 1 : param(0, 1) - 1;
		vga_col = param(1, 1) > VGA_WIDTH ? VGA_WIDTH - 1 : param(1, 1) - 1;
		break;
	case 'A':
		vga_row = n > vga_row ? 0 : vga_row - n;
		break;
	case 'B':
		vga_row = vga_row + n >= VGA_HEIGHT ? VGA_HEIGHT - 1 : vga_row + n;
		break;
	case 'C':
		vga_col = vga_col + n >= VGA_WIDTH ? VGA_WIDTH - 1 : vga_col + n;
		break;
	case 'D':
		vga_col = n > vga_col ? 0 : vga_col - n;
		break;
	case 'J': /* 0: to the end, 1: from the start, 2: all */
		erase(param(0, 0) == 0 ? here : 0,
		      param(0, 0) == 1 ? here + 1 : VGA_WIDTH * VGA_HEIGHT);
		break;
	case 'K': /* the same, within the line */
		erase(param(0, 0) == 0 ? here : vga_row * VGA_WIDTH,
		      param(0, 0) == 1 ? here + 1 : (vga_row + 1) * VGA_WIDTH);
		break;
	}
}

/* Returns whether c belonged to an escape sequence. */
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
		esc_state = c == '[' ? CSI : TEXT; /* other escapes: dropped */
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
		esc_state = TEXT; /* not a sequence after all */
	}
	return true;
}

static void vga_putc(char c)
{
	if (escape(c)) {
		return;
	}
	switch (c) {
	case '\n':
		newline();
		return;
	case '\r':
		vga_col = 0;
		return;
	case '\b':
		if (vga_col > 0) {
			vga_col--;
		}
		return;
	case '\t':
		do {
			vga_putc(' ');
		} while (vga_col % TAB_WIDTH);
		return;
	}

	vga_memory[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, vga_color);
	if (++vga_col == VGA_WIDTH) {
		newline();
	}
}

void vga_write(const char *buf, size_t len)
{
	uint32_t flags = cpu_irq_save();
	for (size_t i = 0; i < len; i++) {
		vga_putc(buf[i]);
	}
	update_cursor();
	cpu_irq_restore(flags);
}

void vga_text_suspend(void)
{
	uint32_t flags = cpu_irq_save();
	if (vga_memory != shadow) {
		for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
			shadow[i] = vga_hardware[i];
		}
		vga_memory = shadow;
	}
	cpu_irq_restore(flags);
}

void vga_text_resume(void)
{
	uint32_t flags = cpu_irq_save();
	if (vga_memory == shadow) {
		for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
			vga_hardware[i] = shadow[i];
		}
		vga_memory = vga_hardware;
		update_cursor();
	}
	cpu_irq_restore(flags);
}

static long vga_dev_write(struct device *dev, const void *buf, size_t len)
{
	(void)dev;
	vga_write(buf, len);
	return (long)len;
}

static const struct char_device_ops vga_ops = { .write = vga_dev_write };
static struct device vga_device = { .name = "vga", .class = DEVICE_CHAR, .char_ops = &vga_ops };

void vga_register(void)
{
	device_register(&vga_device);
}
