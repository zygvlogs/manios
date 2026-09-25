/* VGA hardware state: saving and restoring text mode, and mode 13h
 * (320x200, 256 colours, one byte per pixel at 0xA0000) -- graphics
 * that every VGA card since 1987 can show. Register meanings are from
 * the IBM VGA technical reference; the mode 13h values are the
 * standard, widely published ones.
 *
 * Graphics modes (this one and the Bochs VBE modes) reuse the video
 * memory that text mode keeps its characters and font in, so the text
 * state is saved before and restored after: registers, the DAC
 * palette, and the font in plane 2. (The characters themselves are kept
 * by vga_text.c.) */
#include "vga_hw.h"
#include "io.h"
#include "kstring.h"
#include "memlayout.h"

#define MISC_READ   0x3CC
#define MISC_WRITE  0x3C2
#define SEQ_INDEX   0x3C4
#define SEQ_DATA    0x3C5
#define CRTC_INDEX  0x3D4
#define CRTC_DATA   0x3D5
#define GC_INDEX    0x3CE
#define GC_DATA     0x3CF
#define AC_INDEX    0x3C0
#define AC_READ     0x3C1
#define INPUT_STATUS 0x3DA /* reading it resets the attribute flip-flop */
#define DAC_READ_INDEX  0x3C7
#define DAC_WRITE_INDEX 0x3C8
#define DAC_DATA        0x3C9

#define SEQ_COUNT 5
#define CRTC_COUNT 25
#define GC_COUNT 9
#define AC_COUNT 21
#define FONT_BYTES (256 * 32) /* 32 bytes per character in plane 2 */

struct regs {
	uint8_t misc;
	uint8_t seq[SEQ_COUNT];
	uint8_t crtc[CRTC_COUNT];
	uint8_t gc[GC_COUNT];
	uint8_t ac[AC_COUNT];
};

static struct regs text_regs;
static uint8_t text_dac[256 * 3];
static uint8_t text_font[FONT_BYTES];
static bool saved;

static const struct regs mode13 = {
	.misc = 0x63,
	.seq = { 0x03, 0x01, 0x0F, 0x00, 0x0E },
	.crtc = { 0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, 0x00, 0x41, 0x00, 0x00, 0x00,
	          0x00, 0x00, 0x00, 0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF },
	.gc = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF },
	.ac = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
	        0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x41, 0x00, 0x0F, 0x00, 0x00 },
};

static uint8_t read_indexed(uint16_t index_port, uint16_t data_port, uint8_t index)
{
	outb(index_port, index);
	return inb(data_port);
}

static void write_indexed(uint16_t index_port, uint16_t data_port, uint8_t index, uint8_t v)
{
	outb(index_port, index);
	outb(data_port, v);
}

static void read_regs(struct regs *r)
{
	r->misc = inb(MISC_READ);
	for (uint8_t i = 0; i < SEQ_COUNT; i++) {
		r->seq[i] = read_indexed(SEQ_INDEX, SEQ_DATA, i);
	}
	for (uint8_t i = 0; i < CRTC_COUNT; i++) {
		r->crtc[i] = read_indexed(CRTC_INDEX, CRTC_DATA, i);
	}
	for (uint8_t i = 0; i < GC_COUNT; i++) {
		r->gc[i] = read_indexed(GC_INDEX, GC_DATA, i);
	}
	for (uint8_t i = 0; i < AC_COUNT; i++) {
		inb(INPUT_STATUS);
		outb(AC_INDEX, i);
		r->ac[i] = inb(AC_READ);
	}
	inb(INPUT_STATUS);
	outb(AC_INDEX, 0x20); /* palette access off: the display runs again */
}

static void write_regs(const struct regs *r)
{
	outb(MISC_WRITE, r->misc);
	for (uint8_t i = 0; i < SEQ_COUNT; i++) {
		write_indexed(SEQ_INDEX, SEQ_DATA, i, r->seq[i]);
	}
	/* CRTC registers 0-7 are write-protected by bit 7 of register 0x11. */
	write_indexed(CRTC_INDEX, CRTC_DATA, 0x11, read_indexed(CRTC_INDEX, CRTC_DATA, 0x11) & 0x7F);
	for (uint8_t i = 0; i < CRTC_COUNT; i++) {
		uint8_t v = r->crtc[i];
		if (i == 0x11) {
			v &= 0x7F;
		}
		write_indexed(CRTC_INDEX, CRTC_DATA, i, v);
	}
	write_indexed(CRTC_INDEX, CRTC_DATA, 0x11, r->crtc[0x11]); /* the protection as it was */
	for (uint8_t i = 0; i < GC_COUNT; i++) {
		write_indexed(GC_INDEX, GC_DATA, i, r->gc[i]);
	}
	for (uint8_t i = 0; i < AC_COUNT; i++) {
		inb(INPUT_STATUS);
		outb(AC_INDEX, i);
		outb(AC_INDEX, r->ac[i]);
	}
	inb(INPUT_STATUS);
	outb(AC_INDEX, 0x20);
}

/* Plane 2, where text mode keeps its font, linear at 0xA0000. */
static void select_font_plane(void)
{
	write_indexed(SEQ_INDEX, SEQ_DATA, 0x02, 0x04); /* write plane 2 */
	write_indexed(SEQ_INDEX, SEQ_DATA, 0x04, 0x06); /* sequential, no odd/even */
	write_indexed(GC_INDEX, GC_DATA, 0x04, 0x02);   /* read plane 2 */
	write_indexed(GC_INDEX, GC_DATA, 0x05, 0x00);   /* no odd/even */
	write_indexed(GC_INDEX, GC_DATA, 0x06, 0x04);   /* 0xA0000, 64 KiB */
}

void vga_save_text(void)
{
	read_regs(&text_regs);
	outb(DAC_READ_INDEX, 0);
	for (int i = 0; i < 256 * 3; i++) {
		text_dac[i] = inb(DAC_DATA);
	}
	select_font_plane();
	memcpy(text_font, P2V(0xA0000), FONT_BYTES);
	write_regs(&text_regs);
	saved = true;
}

void vga_restore_text(void)
{
	if (!saved) {
		return;
	}
	select_font_plane();
	memcpy(P2V(0xA0000), text_font, FONT_BYTES);
	write_regs(&text_regs);
	outb(DAC_WRITE_INDEX, 0);
	for (int i = 0; i < 256 * 3; i++) {
		outb(DAC_DATA, text_dac[i]);
	}
}

void vga_set_mode13(void)
{
	write_regs(&mode13);
	/* The palette is RGB 3-3-2, so a pixel byte is its colour. */
	outb(DAC_WRITE_INDEX, 0);
	for (int i = 0; i < 256; i++) {
		outb(DAC_DATA, (uint8_t)(((i >> 5) & 7) * 63 / 7));
		outb(DAC_DATA, (uint8_t)(((i >> 2) & 7) * 63 / 7));
		outb(DAC_DATA, (uint8_t)((i & 3) * 63 / 3));
	}
	memset(P2V(0xA0000), 0, 320 * 200);
}
