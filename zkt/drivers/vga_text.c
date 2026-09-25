#include "vga_text.h"
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

void vga_clear(void)
{
	for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
		vga_memory[i] = vga_entry(' ', VGA_DEFAULT_COLOR);
	}
	vga_row = 0;
	vga_col = 0;
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

static void vga_putc(char c)
{
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

	vga_memory[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, VGA_DEFAULT_COLOR);
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
