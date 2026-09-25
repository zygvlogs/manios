#include "vga_text.h"
#include <stddef.h>
#include <stdint.h>
#include "memlayout.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_DEFAULT_COLOR 0x07 /* light grey on black */
#define VGA_TEXT_PHYS 0xB8000

static uint16_t *const vga_memory = P2V(VGA_TEXT_PHYS);
static size_t vga_row = 0;
static size_t vga_col = 0;

static inline uint16_t vga_entry(char c, uint8_t color)
{
	return (uint16_t)(uint8_t)c | (uint16_t)color << 8;
}

void vga_clear(void)
{
	for (size_t y = 0; y < VGA_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			vga_memory[y * VGA_WIDTH + x] = vga_entry(' ', VGA_DEFAULT_COLOR);
		}
	}
	vga_row = 0;
	vga_col = 0;
}

static void vga_scroll(void)
{
	for (size_t y = 1; y < VGA_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			vga_memory[(y - 1) * VGA_WIDTH + x] = vga_memory[y * VGA_WIDTH + x];
		}
	}
	for (size_t x = 0; x < VGA_WIDTH; x++) {
		vga_memory[(VGA_HEIGHT - 1) * VGA_WIDTH + x] =
		    vga_entry(' ', VGA_DEFAULT_COLOR);
	}
	vga_row = VGA_HEIGHT - 1;
}

void vga_putc(char c)
{
	if (c == '\n') {
		vga_col = 0;
		if (++vga_row == VGA_HEIGHT) {
			vga_scroll();
		}
		return;
	}

	vga_memory[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, VGA_DEFAULT_COLOR);
	if (++vga_col == VGA_WIDTH) {
		vga_col = 0;
		if (++vga_row == VGA_HEIGHT) {
			vga_scroll();
		}
	}
}

void vga_write(const char *s)
{
	for (; *s; s++) {
		vga_putc(*s);
	}
}
