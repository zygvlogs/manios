/* The DOS and BIOS services (dosapi.h). The function numbers and their
 * register conventions follow the MS-DOS 4.0 source's dispatch table
 * (v4.0/src/DOS/DISPATCH.ASM) and its console group
 * (v4.0/src/DOS/CPMIO.ASM), which Microsoft published under the MIT
 * License. The code here is ManiOS's own; the source is a reference for
 * what each function must do, not something copied.
 *
 * A program's files are ManiOS's: a DOS path is mapped onto a ManiOS
 * path by dos_path() (drives.c), so a DOS program reads and writes the
 * same files ManiDOS sees. The console is the terminal dosrun runs on;
 * video mode 13h is ManiOS's /dev/fb.
 */
#include "dosapi.h"
#include "drives.h"

#include <errno.h>
#include <fcntl.h>
#include <gfx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The PSP (Program Segment Prefix) fields we fill in, from the MS-DOS
 * 4.0 source's PDB.INC (MIT). Offsets are the documented ones. */
#define PSP_INT20   0x00 /* INT 20h: terminate */
#define PSP_MEMSIZE 0x02 /* the block's size in paragraphs */
#define PSP_CPM     0x05 /* the far call to CP/M's entry */
#define PSP_INT21   0x0A /* the INT 21h: RETF vector */
#define PSP_INT22   0x0E /* the terminate address */
#define PSP_INT23   0x12 /* the Ctrl-C address */
#define PSP_INT24   0x16 /* the fatal error address */
#define PSP_PARENT  0x18 /* the parent's PSP segment */
#define PSP_JFT     0x18 /* the job file table (20 bytes) */
#define PSP_ENVIRON 0x2C /* the environment's segment */
#define PSP_TAIL    0x80 /* the command tail: length, then the text */
#define PSP_FCB1    0x5C /* the first FCB, from the first argument */
#define PSP_FCB2    0x6C /* the second FCB */
#define PSP_DTA     0x80 /* the default disk transfer area */

/* The open files, one per DOS handle. Handles 0-4 are the standard
 * ones; a program's own files start at 5. */
#define HANDLES 20

struct dos_state {
	struct cpu *cpu;
	uint16_t psp_seg;
	uint16_t prog_seg;
	uint16_t dta;      /* the disk transfer area, DS-relative offset */
	uint8_t exit_code;
	int con_in;        /* the console, for a program's reads */
	int con_out;
	/* The video mode: 3 is text, 13h is 320x200 graphics. */
	int video_mode;
	/* Mode 13h: ManiOS's screen, and whether it is open. */
	struct gfx_screen screen;
	int screen_open;
	/* The open files, one per DOS handle: 0-4 are the standard ones, a
	 * program's own files start at 5. */
	int handle_fd[HANDLES];
};

struct dos_state *dos_new(struct cpu *c)
{
	struct dos_state *d = calloc(1, sizeof(*d));
	if (!d) {
		return NULL;
	}
	d->cpu = c;
	d->con_in = 0;
	d->con_out = 1;
	d->video_mode = 3;
	d->dta = PSP_DTA;
	for (int i = 0; i < HANDLES; i++) {
		d->handle_fd[i] = -1;
	}
	d->handle_fd[0] = 0; /* stdin */
	d->handle_fd[1] = 1; /* stdout */
	d->handle_fd[2] = 2; /* stderr */
	d->handle_fd[3] = 0; /* aux */
	d->handle_fd[4] = 1; /* printer */
	return d;
}

void dos_free(struct dos_state *d)
{
	free(d);
}

void dos_set_program(struct dos_state *d, uint16_t psp_seg, uint16_t prog_seg)
{
	d->psp_seg = psp_seg;
	d->prog_seg = prog_seg;
}

/* --- the PSP --- */

/* The command tail DOS's PSP holds: a length byte, the text (with a
 * leading space), and a CR. */
void dos_set_tail(struct dos_state *d, const char *tail)
{
	struct cpu *c = d->cpu;
	char buf[128];
	int n = 0;
	if (tail && *tail) {
		buf[n++] = ' ';
		for (const char *p = tail; *p && n < 126; p++) {
			buf[n++] = *p;
		}
	}
	buf[n++] = '\r';
	wr8(c, d->psp_seg, PSP_TAIL, (uint8_t)n);
	for (int i = 0; i < n; i++) {
		wr8(c, d->psp_seg, (uint16_t)(PSP_TAIL + 1 + i), (uint8_t)buf[i]);
	}
}

/* Builds the PSP the way DOS does, so a program that looks at it finds
 * what it expects. */
void dos_build_psp(struct dos_state *d)
{
	struct cpu *c = d->cpu;
	uint16_t psp = d->psp_seg;
	/* INT 20h: terminate. */
	wr8(c, psp, PSP_INT20, 0xCD);
	wr8(c, psp, PSP_INT20 + 1, 0x20);
	/* The block's size in paragraphs (the whole 64 KiB segment). */
	wr16(c, psp, PSP_MEMSIZE, 0x1000);
	/* The far call to the CP/M entry, and the INT 21h RETF stub. */
	wr8(c, psp, PSP_CPM, 0xEA);
	wr16(c, psp, PSP_CPM + 1, 0x0000);
	wr16(c, psp, PSP_CPM + 3, 0x0000);
	wr8(c, psp, PSP_INT21, 0xCD);
	wr8(c, psp, PSP_INT21 + 1, 0x21);
	wr8(c, psp, PSP_INT21 + 2, 0xCB); /* RETF */
	/* The terminate, Ctrl-C and fatal-error addresses: a far RET. */
	wr8(c, psp, PSP_INT22, 0xCB);
	wr8(c, psp, PSP_INT23, 0xCB);
	wr8(c, psp, PSP_INT24, 0xCB);
	/* The parent's PSP: none. */
	wr16(c, psp, PSP_PARENT, 0xFFFF);
	/* The job file table: 0-4 are the standard handles, the rest free. */
	for (int i = 0; i < 20; i++) {
		wr8(c, psp, (uint16_t)(PSP_JFT + i), i < 5 ? (uint8_t)i : 0xFF);
	}
	/* The environment's segment: a small empty one. */
	wr16(c, psp, PSP_ENVIRON, 0x0000);
}

static void set_cf(struct cpu *c, int on)
{
	if (on) {
		c->flags |= FLAG_CF;
	} else {
		c->flags &= ~FLAG_CF;
	}
}

/* --- the console --- */

static void con_putc(struct dos_state *d, uint8_t ch)
{
	if (ch == '\n') {
		write(d->con_out, "\r\n", 2);
	} else if (ch == '\r') {
		write(d->con_out, "\r\n", 2);
	} else {
		write(d->con_out, &ch, 1);
	}
}

/* Reads one key, waiting. Returns -1 at end of input. */
static int con_getc(struct dos_state *d)
{
	uint8_t ch;
	for (;;) {
		long n = read(d->con_in, &ch, 1);
		if (n == 1) {
			return ch;
		}
		if (n == 0) {
			return -1;
		}
		if (errno != EINTR) {
			return -1;
		}
	}
}

/* Prints a '$'-terminated string at DS:DX (DOS function 9). */
static void print_string(struct dos_state *d, uint16_t seg, uint16_t off)
{
	struct cpu *c = d->cpu;
	for (int i = 0; i < 65536; i++) {
		uint8_t ch = rd8(c, seg, (uint16_t)(off + i));
		if (ch == '$') {
			break;
		}
		con_putc(d, ch);
	}
}

/* --- DOS functions --- */

/* INT 21h. Returns CPU_OK, or CPU_EXIT for AH=4Ch. */
static enum cpu_stop dos21(struct dos_state *d, struct cpu *c)
{
	uint8_t ah = (uint8_t)(c->r[AX] >> 8);
	uint16_t bx = c->r[BX], dx = c->r[DX];
	uint16_t ds = c->sreg[3];

	switch (ah) {
	case 0x00: /* terminate */
		d->exit_code = 0;
		c->exit_code = 0;
		return CPU_EXIT;

	case 0x01: { /* read a character, echoing */
		int ch = con_getc(d);
		if (ch < 0) {
			ch = 0x1A; /* Ctrl-Z: end of input */
		}
		con_putc(d, (uint8_t)ch);
		c->r[AX] = (uint16_t)((c->r[AX] & 0xFF00) | (uint8_t)ch);
		return CPU_OK;
	}

	case 0x02: /* write a character */
		con_putc(d, (uint8_t)(dx & 0xFF));
		c->r[AX] = (uint16_t)((c->r[AX] & 0xFF00) | (uint8_t)(dx & 0xFF));
		return CPU_OK;

	case 0x06: { /* direct console I/O */
		uint8_t dl = (uint8_t)(dx & 0xFF);
		if (dl == 0xFF) {
			/* Input, no echo, non-blocking: ZF set if none ready. */
			c->flags |= FLAG_ZF;
			c->r[AX] = (uint16_t)(c->r[AX] & 0xFF00);
		} else {
			con_putc(d, dl);
		}
		return CPU_OK;
	}

	case 0x07: case 0x08: { /* read a character, no echo */
		int ch = con_getc(d);
		if (ch < 0) {
			ch = 0x1A;
		}
		c->r[AX] = (uint16_t)((c->r[AX] & 0xFF00) | (uint8_t)ch);
		return CPU_OK;
	}

	case 0x09: /* print a '$'-terminated string */
		print_string(d, ds, dx);
		return CPU_OK;

	case 0x0A: { /* buffered input */
		uint8_t max = rd8(c, ds, dx);
		int n = 0;
		for (;;) {
			int ch = con_getc(d);
			if (ch < 0 || ch == '\n' || ch == '\r') {
				break;
			}
			if (ch == '\b') {
				if (n > 0) {
					n--;
					con_putc(d, '\b');
					con_putc(d, ' ');
					con_putc(d, '\b');
				}
				continue;
			}
			if (n < max - 1) {
				wr8(c, ds, (uint16_t)(dx + 2 + n), (uint8_t)ch);
				n++;
				con_putc(d, (uint8_t)ch);
			}
		}
		wr8(c, ds, (uint16_t)(dx + 1), (uint8_t)n);
		wr8(c, ds, (uint16_t)(dx + 2 + n), '\r');
		con_putc(d, '\n');
		return CPU_OK;
	}

	case 0x0B: /* console input status */
		c->r[AX] = (uint16_t)((c->r[AX] & 0xFF00) | 0x00);
		return CPU_OK;

	case 0x0C: { /* flush the input buffer, then do AL's function */
		/* There is no unread input to flush (a read waits), so just
		 * run the function AL names, as DOS does after the flush. */
		uint8_t al = (uint8_t)(c->r[AX] & 0xFF);
		c->r[AX] = (uint16_t)((c->r[AX] & 0xFF00) | al);
		if (al == 0x01 || al == 0x06 || al == 0x07 || al == 0x08 || al == 0x0A) {
			return dos21(d, c);
		}
		return CPU_OK;
	}

	case 0x0D: /* disk reset: nothing to do */
		return CPU_OK;

	case 0x0E: /* select the default drive */
		c->r[AX] = (uint16_t)((c->r[AX] & 0xFF00) | 0x1F); /* 32 drives */
		return CPU_OK;

	case 0x19: /* the current drive */
		c->r[AX] = (uint16_t)((c->r[AX] & 0xFF00) | 0x02); /* C: */
		return CPU_OK;

	case 0x1A: /* set the disk transfer area */
		d->dta = dx;
		return CPU_OK;

	case 0x25: { /* set an interrupt vector */
		uint8_t vec = (uint8_t)(c->r[AX] & 0xFF);
		wr16(c, 0, (uint16_t)(vec * 4), dx);
		wr16(c, 0, (uint16_t)(vec * 4 + 2), ds);
		return CPU_OK;
	}

	case 0x2A: { /* the date: CX=year, DH=month, DL=day, AL=weekday */
		c->r[CX] = 2026;
		c->r[DX] = (uint16_t)((10 << 8) | 7);
		c->r[AX] = (uint16_t)((c->r[AX] & 0xFF00) | 2); /* Wednesday */
		return CPU_OK;
	}

	case 0x2C: { /* the time: CH=hour, CL=minute, DH=second, DL=centisecond */
		c->r[CX] = (uint16_t)((12 << 8) | 0);
		c->r[DX] = (uint16_t)((0 << 8) | 0);
		return CPU_OK;
	}

	case 0x2F: /* get the disk transfer area: ES:BX */
		c->sreg[0] = ds;
		c->r[BX] = d->dta;
		return CPU_OK;

	case 0x30: /* the DOS version: 4.00 */
		c->r[AX] = 0x0004;
		c->r[BX] = 0x0000;
		c->r[CX] = 0x0000;
		return CPU_OK;

	case 0x33: /* Ctrl-C checking: off */
		c->r[DX] = (uint16_t)((c->r[DX] & 0xFF00) | 0x00);
		return CPU_OK;

	case 0x35: { /* get an interrupt vector: ES:BX */
		uint8_t vec = (uint8_t)(c->r[AX] & 0xFF);
		c->r[BX] = rd16(c, 0, (uint16_t)(vec * 4));
		c->sreg[0] = rd16(c, 0, (uint16_t)(vec * 4 + 2));
		return CPU_OK;
	}

	case 0x36: { /* free space: AX=sectors/cluster, BX=free, CX=bytes/sector */
		c->r[AX] = 4;
		c->r[BX] = 0x1000;
		c->r[CX] = 512;
		c->r[DX] = 0x1000;
		return CPU_OK;
	}

	case 0x3B: /* change directory */
	case 0x39: /* make directory */
	case 0x3A: /* remove directory */
		set_cf(c, 1);
		c->r[AX] = 0x0003; /* path not found */
		return CPU_OK;

	case 0x3C: case 0x3D: case 0x3E: case 0x3F: case 0x40: case 0x41:
	case 0x42: case 0x43: case 0x4E: case 0x4F:
		return dos_file(d, c, ah);

	case 0x44: /* IOCTL: a device, or a file handle */
		if (bx == 0) {
			c->r[DX] = (uint16_t)((c->r[DX] & 0xFF00) | 0x80); /* a device */
			c->r[AX] = (uint16_t)((c->r[AX] & 0xFF00) | 0x01);
		} else {
			c->r[DX] = (uint16_t)(c->r[DX] & 0xFF00);
		}
		return CPU_OK;

	case 0x4C: /* terminate with a return code */
		d->exit_code = (uint8_t)(c->r[AX] & 0xFF);
		c->exit_code = d->exit_code;
		return CPU_EXIT;

	case 0x4D: /* get the return code: nothing yet */
		c->r[AX] = (uint16_t)(c->r[AX] & 0xFF00);
		return CPU_OK;

	case 0x51: case 0x62: /* get the current PSP */
		c->r[BX] = d->psp_seg;
		return CPU_OK;

	default:
		/* A function we don't have: refuse it the way DOS does. */
		set_cf(c, 1);
		c->r[AX] = 0x0001; /* invalid function */
		return CPU_OK;
	}
}

/* --- the file services --- */

static int handle_alloc(struct dos_state *d, int fd)
{
	for (int i = 5; i < HANDLES; i++) {
		if (d->handle_fd[i] < 0) {
			d->handle_fd[i] = fd;
			return i;
		}
	}
	return -1;
}

/* The DOS path at DS:DX, as a ManiOS path (drives.c). */
static int path_at(struct dos_state *d, uint16_t seg, uint16_t off, char *out, size_t n)
{
	struct cpu *c = d->cpu;
	size_t i = 0;
	for (; i + 1 < n; i++) {
		uint8_t ch = rd8(c, seg, (uint16_t)(off + i));
		if (ch == 0) {
			break;
		}
		out[i] = (char)ch;
	}
	out[i] = '\0';
	return dos_path(out, n);
}

/* The file services (INT 21h AH=3C..43, 4E, 4F). */
enum cpu_stop dos_file(struct dos_state *d, struct cpu *c, uint8_t ah)
{
	uint16_t dx = c->r[DX], cx = c->r[CX], bx = c->r[BX];
	uint16_t ds = c->sreg[3];
	char path[256];
	int fd;

	switch (ah) {
	case 0x3C: { /* create */
		if (path_at(d, ds, dx, path, sizeof(path)) < 0) {
			break;
		}
		fd = open(path, O_WRONLY | O_TRUNC);
		if (fd < 0) {
			break;
		}
		int h = handle_alloc(d, fd);
		if (h < 0) {
			close(fd);
			break;
		}
		c->r[AX] = (uint16_t)h;
		set_cf(c, 0);
		return CPU_OK;
	}
	case 0x3D: { /* open */
		if (path_at(d, ds, dx, path, sizeof(path)) < 0) {
			break;
		}
		int mode = (c->r[AX] & 0xFF) == 0 ? O_RDONLY : O_RDWR;
		fd = open(path, mode);
		if (fd < 0) {
			break;
		}
		int h = handle_alloc(d, fd);
		if (h < 0) {
			close(fd);
			break;
		}
		c->r[AX] = (uint16_t)h;
		set_cf(c, 0);
		return CPU_OK;
	}
	case 0x3E: { /* close */
		
		if (bx < HANDLES && d->handle_fd[bx] >= 0 && bx > 4) {
			close(d->handle_fd[bx]);
			d->handle_fd[bx] = -1;
		}
		set_cf(c, 0);
		return CPU_OK;
	}
	case 0x3F: { /* read */
		
		if (bx >= HANDLES || d->handle_fd[bx] < 0) {
			break;
		}
		uint16_t n = cx;
		uint8_t *buf = malloc(n ? n : 1);
		if (!buf) {
			break;
		}
		long got = read(d->handle_fd[bx], buf, n);
		if (got < 0) {
			free(buf);
			break;
		}
		for (long i = 0; i < got; i++) {
			wr8(c, ds, (uint16_t)(dx + i), buf[i]);
		}
		free(buf);
		c->r[AX] = (uint16_t)got;
		set_cf(c, 0);
		return CPU_OK;
	}
	case 0x40: { /* write */
		
		if (bx >= HANDLES || d->handle_fd[bx] < 0) {
			break;
		}
		uint16_t n = cx;
		uint8_t *buf = malloc(n ? n : 1);
		if (!buf) {
			break;
		}
		for (uint16_t i = 0; i < n; i++) {
			buf[i] = rd8(c, ds, (uint16_t)(dx + i));
		}
		long put = write(d->handle_fd[bx], buf, n);
		free(buf);
		if (put < 0) {
			break;
		}
		c->r[AX] = (uint16_t)put;
		set_cf(c, 0);
		return CPU_OK;
	}
	case 0x41: { /* unlink */
		if (path_at(d, ds, dx, path, sizeof(path)) < 0) {
			break;
		}
		if (unlink(path) < 0) {
			break;
		}
		set_cf(c, 0);
		return CPU_OK;
	}
	case 0x42: { /* lseek: CX:DX is the offset, AL the whence */
		
		if (bx >= HANDLES || d->handle_fd[bx] < 0) {
			break;
		}
		int whence = c->r[AX] & 0xFF;
		long off = (long)((uint32_t)cx << 16 | dx);
		long pos = lseek(d->handle_fd[bx], off, whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END);
		if (pos < 0) {
			break;
		}
		c->r[AX] = (uint16_t)(pos & 0xFFFF);
		c->r[DX] = (uint16_t)((pos >> 16) & 0xFFFF);
		set_cf(c, 0);
		return CPU_OK;
	}
	case 0x43: { /* get/set the file attributes */
		if ((c->r[AX] & 0xFF) == 0) {
			c->r[CX] = 0x0020; /* archive */
			set_cf(c, 0);
			return CPU_OK;
		}
		set_cf(c, 0);
		return CPU_OK;
	}
	case 0x4E: case 0x4F: { /* find first / find next: no matches */
		c->r[AX] = 0x0012; /* no more files */
		set_cf(c, 1);
		return CPU_OK;
	}
	}
	set_cf(c, 1);
	c->r[AX] = 0x0002; /* file not found */
	return CPU_OK;
}

/* --- the video --- */

/* DOS's mode 13h framebuffer is 320x200 bytes at physical 0xA0000, one
 * byte per pixel, RGB 3-3-2. ManiOS's VGA mode 13h is the same, so a
 * program's writes to 0xA0000 are copied to ManiOS's screen. */
#define VGA_BASE 0xA0000
#define VGA_W 320
#define VGA_H 200

void dos_video_mode13(struct dos_state *d)
{
	if (d->screen_open) {
		return;
	}
	if (gfx_screen_open(&d->screen, 0, 0) == 0) {
		d->screen_open = 1;
	}
}

void dos_video_text(struct dos_state *d)
{
	if (d->screen_open) {
		gfx_screen_close(&d->screen);
		d->screen_open = 0;
	}
}

/* Copies the program's mode 13h framebuffer to ManiOS's screen. The
 * program's pixels are one byte each (RGB 3-3-2); ManiOS's canvas is
 * 32-bit, so each byte is expanded to a colour. */
void dos_video_present(struct dos_state *d)
{
	if (!d->screen_open) {
		return;
	}
	static uint32_t pixels[VGA_W * VGA_H];
	const uint8_t *fb = d->cpu->mem + VGA_BASE;
	for (int i = 0; i < VGA_W * VGA_H; i++) {
		uint8_t p = fb[i];
		uint32_t r = (uint32_t)(p >> 5) * 255 / 7;
		uint32_t g = (uint32_t)((p >> 2) & 7) * 255 / 7;
		uint32_t b = (uint32_t)(p & 3) * 255 / 3;
		pixels[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
	}
	struct gfx_canvas c = {
		.width = VGA_W,
		.height = VGA_H,
		.pixels = pixels,
		.clip = { 0, 0, VGA_W, VGA_H },
	};
	struct gfx_rect r = { 0, 0, VGA_W, VGA_H };
	gfx_present(&d->screen, &c, r);
}

/* --- BIOS --- */

/* INT 10h: the video services. Text mode is the terminal; mode 13h is
 * ManiOS's framebuffer. */
static enum cpu_stop bios10(struct dos_state *d, struct cpu *c)
{
	uint8_t ah = (uint8_t)(c->r[AX] >> 8);
	uint8_t al = (uint8_t)(c->r[AX] & 0xFF);
	switch (ah) {
	case 0x00: /* set the video mode */
		d->video_mode = al;
		if (al == 0x13) {
			/* 320x200, 256 colours: ManiOS's VGA mode 13h. */
			dos_video_mode13(d);
		} else if (al == 0x03) {
			dos_video_text(d);
		}
		return CPU_OK;
	case 0x01: case 0x02: case 0x03: case 0x05: case 0x06:
	case 0x07: case 0x08: case 0x09: case 0x0A: case 0x0C:
	case 0x0D: case 0x0E: case 0x0F:
		return CPU_OK; /* text-mode cursor and character services */
	case 0x10: case 0x11: case 0x12: case 0x1A: case 0x1B:
		return CPU_OK; /* the palette, font and adapter services */
	default:
		return CPU_OK;
	}
}

/* INT 16h: the keyboard. */
static enum cpu_stop bios16(struct dos_state *d, struct cpu *c)
{
	uint8_t ah = (uint8_t)(c->r[AX] >> 8);
	switch (ah) {
	case 0x00: case 0x10: { /* read a key: AH=scan, AL=ASCII */
		int ch = con_getc(d);
		if (ch < 0) {
			ch = 0x1A;
		}
		c->r[AX] = (uint16_t)(((uint16_t)0 << 8) | (uint8_t)ch);
		return CPU_OK;
	}
	case 0x01: case 0x11: /* key ready? ZF set if not */
		c->flags |= FLAG_ZF;
		c->r[AX] = 0;
		return CPU_OK;
	case 0x02: /* shift status */
		c->r[AX] = (uint16_t)(c->r[AX] & 0xFF00);
		return CPU_OK;
	default:
		return CPU_OK;
	}
}

/* INT 1Ah: the clock. */
static enum cpu_stop bios1a(struct dos_state *d, struct cpu *c)
{
	(void)d;
	uint8_t ah = (uint8_t)(c->r[AX] >> 8);
	switch (ah) {
	case 0x00: /* read the tick count: CX:DX, AL rolled over */
		c->r[CX] = 0;
		c->r[DX] = 0;
		c->r[AX] = (uint16_t)(c->r[AX] & 0xFF00);
		return CPU_OK;
	case 0x02: /* read the RTC time: CH=hour, CL=minute, DH=second */
		c->r[CX] = (uint16_t)((12 << 8) | 0);
		c->r[DX] = (uint16_t)((0 << 8) | 0);
		set_cf(c, 0);
		return CPU_OK;
	default:
		return CPU_OK;
	}
}

enum cpu_stop dos_int(struct dos_state *d, struct cpu *c, uint8_t vector)
{
	switch (vector) {
	case 0x20: /* terminate */
		c->exit_code = 0;
		return CPU_EXIT;
	case 0x21:
		return dos21(d, c);
	case 0x10:
		return bios10(d, c);
	case 0x16:
		return bios16(d, c);
	case 0x1A:
		return bios1a(d, c);
	case 0x11: case 0x12: case 0x14: case 0x15: case 0x17:
		/* The equipment, memory size, serial, cassette and printer
		 * services: a program that probes them gets a plain answer. */
		c->r[AX] = (uint16_t)(c->r[AX] & 0xFF00);
		return CPU_OK;
	default:
		/* An unhandled interrupt: return as an IRET would. */
		return CPU_IRET;
	}
}
