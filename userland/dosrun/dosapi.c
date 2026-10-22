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
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <gfx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef MANIOS_VERSION
#include <zkt_abi.h>
extern int poll(struct zkt_pollfd *, int, int);
#else
#include <poll.h>
#endif

/* The PSP (Program Segment Prefix) fields we fill in, from the MS-DOS
 * 4.0 source's PDB.INC (MIT). Offsets are the documented ones. */
#define PSP_INT20   0x00 /* INT 20h: terminate */
#define PSP_MEMSIZE 0x02 /* segment immediately beyond the allocated block */
#define PSP_CPM     0x05 /* the far call to CP/M's entry */
#define PSP_INT21   0x50 /* the INT 21h: RETF vector */
#define PSP_INT22   0x0A /* the terminate address */
#define PSP_INT23   0x0E /* the Ctrl-C address */
#define PSP_INT24   0x12 /* the fatal error address */
#define PSP_PARENT  0x16 /* the parent's PSP segment */
#define PSP_JFT     0x18 /* the job file table (20 bytes) */
#define PSP_ENVIRON 0x2C /* the environment's segment */
#define PSP_TAIL    0x80 /* the command tail: length, then the text */
#define PSP_FCB1    0x5C /* the first FCB, from the first argument */
#define PSP_FCB2    0x6C /* the second FCB */
#define PSP_DTA     0x80 /* the default disk transfer area */

/* The open files, one per DOS handle. Handles 0-4 are the standard
 * ones; a program's own files start at 5. */
#define HANDLES 20

/* Extended memory (XMS) and expanded memory (EMS): the pools a DOS
 * program asks for beyond the 1 MiB it can address. The sizes are
 * modest, enough for a game's sound and music buffers. */
#define XMS_BYTES   (8u * 1024 * 1024)
#define XMS_HANDLES 32
#define EMS_BYTES   (4u * 1024 * 1024)
#define EMS_HANDLES 32
#define EMS_PAGE    16384u /* a 16 KiB page, as EMS has it */
#define EMS_FRAME   0xE000 /* where the 4-page window is mapped */

struct dos_state {
	struct cpu *cpu;
	uint16_t psp_seg;
	uint16_t prog_seg;
	uint16_t dta, dta_seg; /* disk transfer area, independent of later DS */
	struct { DIR *dir; uint16_t seg, off, attr; char root[256], pattern[128]; } find[8];
	uint8_t exit_code;
	int con_in;        /* the console, for a program's reads */
	int con_out;
	int pending_key;
	int fcb_fd[16];
	/* The video mode: 3 is text, 13h is 320x200 graphics. */
	int video_mode;
	uint16_t cursor[8], cursor_shape;
	uint8_t active_page;
	/* Mode 13h: ManiOS's screen, and whether it is open. */
	struct gfx_screen screen;
	int screen_open;
	/* The open files, one per DOS handle: 0-4 are the standard ones, a
	 * program's own files start at 5. */
	int handle_fd[HANDLES];

	/* Extended memory (XMS): a pool outside the 1 MiB the program can
	 * address, which it copies to and from through a move call. */
	uint8_t *xms;
	uint32_t xms_size;
	/* The XMS handles: 0 is free, otherwise the block's byte offset. */
	uint32_t xms_handle[XMS_HANDLES];
	uint32_t xms_len[XMS_HANDLES];
	int xms_installed;
	int xms_open; /* the driver is open (AH=43h AL=10h) */

	/* Expanded memory (EMS): 64 KiB pages mapped into a window. */
	uint8_t *ems;
	uint32_t ems_pages;
	uint32_t ems_handle[EMS_HANDLES];
	uint32_t ems_pages_of[EMS_HANDLES];
	uint16_t ems_frame; /* the segment of the 4-page window */
	/* What each of the frame's 4 physical pages shows: the handle and
	 * the logical page, or -1 for nothing. The frame is a window, so
	 * its contents are written back to the page before a remap. */
	int ems_map_h[4];
	int ems_map_p[4];
	int ems_open;

};

struct dos_state *dos_new(struct cpu *c)
{
	struct dos_state *d = calloc(1, sizeof(*d));
	if (!d) {
		return NULL;
	}
	d->cpu = c;
	d->pending_key = -1;
	for (int i = 0; i < 16; i++) d->fcb_fd[i] = -1;
	d->con_in = 0;
	d->con_out = 1;
	d->video_mode = 3;
	d->cursor_shape = 0x0607;
	d->dta = PSP_DTA;
	for (int i = 0; i < HANDLES; i++) {
		d->handle_fd[i] = -1;
	}
	d->handle_fd[0] = 0; /* stdin */
	d->handle_fd[1] = 1; /* stdout */
	d->handle_fd[2] = 2; /* stderr */
	d->handle_fd[3] = 0; /* aux */
	d->handle_fd[4] = 1; /* printer */

	/* The extended and expanded memory pools. If they can't be had,
	 * the services report themselves absent, which a program handles. */
	d->xms = calloc(1, XMS_BYTES);
	if (d->xms) {
		d->xms_size = XMS_BYTES;
		d->xms_installed = 1;
	}
	d->ems = calloc(1, EMS_BYTES);
	if (d->ems) {
		d->ems_pages = EMS_BYTES / EMS_PAGE;
		d->ems_frame = EMS_FRAME;
	}
	for (int i = 0; i < 4; i++) {
		d->ems_map_h[i] = -1;
		d->ems_map_p[i] = -1;
	}
	return d;
}

void dos_free(struct dos_state *d)
{
	for (int i = 0; i < 16; i++) if (d->fcb_fd[i] >= 0) close(d->fcb_fd[i]);
	for (int i = 0; i < 8; i++) if (d->find[i].dir) closedir(d->find[i].dir);
	for (int i = 0; i < HANDLES; i++) if (d->handle_fd[i] > 2) close(d->handle_fd[i]);
	free(d->xms);
	free(d->ems);
	free(d);
}

void dos_set_program(struct dos_state *d, uint16_t psp_seg, uint16_t prog_seg)
{
	d->psp_seg = psp_seg;
	d->prog_seg = prog_seg;
	d->dta_seg = psp_seg;
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
	buf[n] = '\r';
	wr8(c, d->psp_seg, PSP_TAIL, (uint8_t)n);
	for (int i = 0; i <= n; i++) {
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
	/* PSP:02 is an ending segment, not a paragraph count. The loader
	 * gives this single DOS process conventional memory below VGA. */
	wr16(c, psp, PSP_MEMSIZE, 0xA000);
	/* The far call to the CP/M entry, and the INT 21h RETF stub. */
	wr8(c, psp, PSP_CPM, 0xEA);
	wr16(c, psp, PSP_CPM + 1, 0x0000);
	wr16(c, psp, PSP_CPM + 3, 0x0000);
	wr8(c, psp, PSP_INT21, 0xCD);
	wr8(c, psp, PSP_INT21 + 1, 0x21);
	wr8(c, psp, PSP_INT21 + 2, 0xCB); /* RETF */
	/* Saved interrupt vectors, each a far pointer. */
	for (int i = 0; i < 3; i++) {
		wr16(c, psp, PSP_INT22 + i * 4, rd16(c, 0, (0x22 + i) * 4));
		wr16(c, psp, PSP_INT22 + i * 4 + 2, rd16(c, 0, (0x22 + i) * 4 + 2));
	}
	/* The parent's PSP: none. */
	wr16(c, psp, PSP_PARENT, 0xFFFF);
	/* The job file table: 0-4 are the standard handles, the rest free. */
	for (int i = 0; i < 20; i++) {
		wr8(c, psp, (uint16_t)(PSP_JFT + i), i < 5 ? (uint8_t)i : 0xFF);
	}
	/* A real, terminated environment below the process allocation. The
	 * word after the double NUL counts executable-name strings. */
	const uint16_t env = 0x0F00;
	const char vars[] = "PATH=C:\\\0COMSPEC=C:\\COMMAND.COM\0";
	for (unsigned i = 0; i < sizeof(vars); i++) wr8(c, env, i, vars[i]);
	wr16(c, env, sizeof(vars), 1);
	const char program[] = "C:\\DOSRUN.EXE";
	for (unsigned i = 0; i < sizeof(program); i++) wr8(c, env, sizeof(vars) + 2 + i, program[i]);
	wr16(c, psp, PSP_ENVIRON, env);
	/* Initially the process owns all remaining conventional memory. */
	wr8(c, psp - 1, 0, 'Z');
	wr16(c, psp - 1, 1, psp);
	wr16(c, psp - 1, 3, 0xA000 - psp);
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

/* DOS writes CR and LF independently. A carriage return moves to column
 * zero without adding a line; the following LF advances the row. */
static void con_putc(struct dos_state *d, uint8_t ch)
{
	uint16_t pos = d->cursor[d->active_page];
	unsigned row = pos >> 8, col = pos & 255;
	if (ch == '\r') col = 0;
	else if (ch == '\n') { if (row < 24) row++; }
	else if (ch == '\b') { if (col) col--; }
	else if (ch >= 32) {
		mem_w16(d->cpu, 0xB8000 + d->active_page * 4096 + (row * 80 + col) * 2, ch | 0x0700);
		if (++col >= 80) { col = 0; if (row < 24) row++; }
	}
	d->cursor[d->active_page] = (row << 8) | col;
	write(d->con_out, &ch, 1);
}

/* Cache a polled byte: BIOS readiness queries must not consume it. */
static int con_peek(struct dos_state *d)
{
	if (d->pending_key >= 0) return d->pending_key;
#ifdef MANIOS_VERSION
	struct zkt_pollfd fd = { d->con_in, 0 };
	if (poll(&fd, 1, 0) <= 0 || !fd.ready) return -1;
#else
	struct pollfd fd = { d->con_in, POLLIN, 0 };
	if (poll(&fd, 1, 0) <= 0 || !(fd.revents & POLLIN)) return -1;
#endif
	uint8_t ch;
	if (read(d->con_in, &ch, 1) != 1) return -1;
	d->pending_key = ch;
	return ch;
}

static uint16_t key_word(int ch)
{
	static const uint8_t letters[] = {30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44};
	int lower = tolower((unsigned char)ch), scan = 0;
	if (lower >= 'a' && lower <= 'z') scan = letters[lower - 'a'];
	else if (ch >= '1' && ch <= '9') scan = ch - '1' + 2;
	else switch (ch) {
	case '0': scan = 11; break; case 27: scan = 1; break;
	case '\r': case '\n': scan = 28; ch = '\r'; break;
	case '\b': scan = 14; break; case '\t': scan = 15; break;
	case ' ': scan = 57; break;
	}
	return (scan << 8) | (uint8_t)ch;
}

/* Reads one key, waiting. Returns -1 at end of input. */
static int con_getc(struct dos_state *d)
{
	if (d->pending_key >= 0) { int ch = d->pending_key; d->pending_key = -1; return ch == '\n' ? '\r' : ch; }
	uint8_t ch;
	for (;;) {
		long n = read(d->con_in, &ch, 1);
		if (n == 1) {
			return ch == '\n' ? '\r' : ch;
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

/* DOS conventional-memory blocks. Each MCB occupies one paragraph directly
 * before its data; its size excludes that header. Blocks end below VGA. */
static int mcb_valid(struct cpu *c, uint16_t m)
{
	uint8_t type = rd8(c, m, 0);
	uint32_t end = (uint32_t)m + 1 + rd16(c, m, 3);
	return (type == 'M' || type == 'Z') && end <= 0xA000 &&
	       (type != 'Z' || end == 0xA000);
}

static void mcb_join(struct cpu *c, uint16_t m)
{
	while (rd8(c, m, 0) == 'M') {
		uint16_t next = m + 1 + rd16(c, m, 3);
		if (!mcb_valid(c, next) || rd16(c, next, 1)) break;
		wr16(c, m, 3, rd16(c, m, 3) + 1 + rd16(c, next, 3));
		wr8(c, m, 0, rd8(c, next, 0));
	}
}

static void mcb_split(struct cpu *c, uint16_t m, uint16_t wanted)
{
	uint16_t size = rd16(c, m, 3);
	if (wanted >= size) return;
	uint16_t next = m + 1 + wanted;
	wr8(c, next, 0, rd8(c, m, 0));
	wr16(c, next, 1, 0);
	wr16(c, next, 3, size - wanted - 1);
	wr8(c, m, 0, 'M');
	wr16(c, m, 3, wanted);
	mcb_join(c, next);
}

static enum cpu_stop dos_memory(struct dos_state *d, struct cpu *c, uint8_t ah)
{
	uint16_t wanted = c->r[BX], largest = 0;
	for (uint32_t at = d->psp_seg - 1; at < 0xA000;) {
		uint16_t m = at;
		if (!mcb_valid(c, m)) { c->r[AX] = 7; set_cf(c, 1); return CPU_OK; }
		if (ah == 0x48 && !rd16(c, m, 1)) {
			mcb_join(c, m);
			uint16_t size = rd16(c, m, 3);
			if (size > largest) largest = size;
			if (wanted && size >= wanted) {
				mcb_split(c, m, wanted); wr16(c, m, 1, d->psp_seg);
				c->r[AX] = m + 1; set_cf(c, 0); return CPU_OK;
			}
		} else if (ah != 0x48 && m + 1 == c->sreg[0] && rd16(c, m, 1)) {
			if (ah == 0x49) {
				wr16(c, m, 1, 0); mcb_join(c, m);
				set_cf(c, 0); return CPU_OK;
			}
			mcb_join(c, m);
			uint16_t size = rd16(c, m, 3);
			if (size < wanted) {
				c->r[AX] = 8; c->r[BX] = size; set_cf(c, 1); return CPU_OK;
			}
			mcb_split(c, m, wanted);
			if (m + 1 == d->psp_seg) wr16(c, d->psp_seg, PSP_MEMSIZE, m + 1 + wanted);
			set_cf(c, 0); return CPU_OK;
		}
		if (rd8(c, m, 0) == 'Z') break;
		at += 1u + rd16(c, m, 3);
	}
	c->r[AX] = ah == 0x48 ? 8 : 9;
	if (ah == 0x48) c->r[BX] = largest;
	set_cf(c, 1); return CPU_OK;
}

/* Legacy FCB record I/O, used by pre-handle runtimes and overlay loaders.
 * Layout and record updates follow the MS-DOS Programmer's Reference:
 * https://www.pcjs.org/documents/books/mspl13/msdos/dosref40/ */
static enum cpu_stop dos_fcb(struct dos_state *d, struct cpu *c, uint8_t ah)
{
	uint16_t seg = c->sreg[3], off = c->r[DX];
	if (rd8(c, seg, off) == 0xFF) off += 7; /* extended FCB */
	uint8_t status = 0xFF;
	int slot = (int)rd16(c, seg, off + 0x18) - 1;
	if (rd16(c, seg, off + 0x1A) != 0x4D46 || slot < 0 || slot >= 16 || d->fcb_fd[slot] < 0) slot = -1;
	if (ah == 0x0F || ah == 0x16) {
		char path[256], name[16]; int n = 0;
		int drive = rd8(c, seg, off);
		if (drive > 26) goto done;
		name[n++] = 'A' + (drive ? drive - 1 : dos_get_drive()); name[n++] = ':'; name[n++] = '/';
		for (int i = 0; i < 8; i++) { uint8_t ch = rd8(c, seg, off + 1 + i); if (ch && ch != ' ') name[n++] = ch; }
		if (rd8(c, seg, off + 9) != ' ' && rd8(c, seg, off + 9)) {
			name[n++] = '.';
			for (int i = 0; i < 3; i++) { uint8_t ch = rd8(c, seg, off + 9 + i); if (ch && ch != ' ') name[n++] = ch; }
		}
		name[n] = 0; snprintf(path, sizeof(path), "%s", name);
		if (dos_path(path, sizeof(path)) < 0) goto done;
		/* Reserve a slot before O_TRUNC can modify an existing file. */
		if (slot < 0) for (int i = 0; i < 16; i++) if (d->fcb_fd[i] < 0) { slot = i; break; }
		if (slot < 0) goto done;
		int fd = ah == 0x16 ? open(path, O_RDWR | O_CREAT | O_TRUNC, 0644) : open(path, O_RDWR);
		if (fd < 0 && ah == 0x0F) fd = open(path, O_RDONLY);
		if (fd < 0) goto done;
		struct stat st;
		if (fstat(fd, &st) < 0) { close(fd); goto done; }
		if (d->fcb_fd[slot] >= 0) close(d->fcb_fd[slot]);
		d->fcb_fd[slot] = fd;
		wr8(c, seg, off, drive ? drive : dos_get_drive() + 1);
		wr16(c, seg, off + 0x0C, 0); wr16(c, seg, off + 0x0E, 128);
		wr16(c, seg, off + 0x10, (uint32_t)st.st_size);
		wr16(c, seg, off + 0x12, (uint32_t)st.st_size >> 16);
		wr16(c, seg, off + 0x14, 0x21); wr16(c, seg, off + 0x16, 0);
		wr16(c, seg, off + 0x18, slot + 1); wr16(c, seg, off + 0x1A, 0x4D46);
		status = 0;
	} else if (ah == 0x24) {
		uint32_t record = (uint32_t)rd16(c, seg, off + 0x0C) * 128 + rd8(c, seg, off + 0x20);
		wr16(c, seg, off + 0x21, record); wr16(c, seg, off + 0x23, record >> 16);
		status = c->r[AX] & 255;
	} else if (slot >= 0 && ah == 0x10) {
		close(d->fcb_fd[slot]); d->fcb_fd[slot] = -1; status = 0;
	} else if (slot >= 0 && (ah == 0x14 || ah == 0x15 || ah == 0x21 || ah == 0x22 || ah == 0x27 || ah == 0x28)) {
		uint16_t size = rd16(c, seg, off + 0x0E);
		int block = ah == 0x27 || ah == 0x28;
		int sequential = ah == 0x14 || ah == 0x15;
		int writing = ah == 0x15 || ah == 0x22 || ah == 0x28;
		uint32_t count = block ? c->r[CX] : 1;
		uint32_t record = sequential ? (uint32_t)rd16(c, seg, off + 0x0C) * 128 + rd8(c, seg, off + 0x20)
		                : rd16(c, seg, off + 0x21) | ((uint32_t)rd16(c, seg, off + 0x23) << 16);
		if (size >= 64 && !sequential) record &= 0xFFFFFF;
		if (block) c->r[CX] = 0;
		if (!size || count * size > 65536u - d->dta || (uint64_t)record * size > 0x7FFFFFFF) { status = 2; goto done; }
		if (!count) {
			/* Zero-count block writes request truncation, which the
			 * current host filesystem API cannot perform on an open fd. */
			status = writing ? 1 : 0; goto done;
		}
		if (lseek(d->fcb_fd[slot], (long)(record * size), SEEK_SET) < 0) { status = 1; goto done; }
		uint32_t bytes = count * size; uint8_t *buf = malloc(bytes);
		if (!buf) { status = 1; goto done; }
		if (writing) for (uint32_t i = 0; i < bytes; i++) buf[i] = rd8(c, d->dta_seg, d->dta + i);
		long got = writing ? write(d->fcb_fd[slot], buf, bytes) : read(d->fcb_fd[slot], buf, bytes);
		if (got < 0) { free(buf); status = 1; goto done; }
		uint32_t records = writing ? (uint32_t)got / size : ((uint32_t)got + size - 1) / size;
		if (!writing) for (uint32_t i = 0; i < records * size; i++) wr8(c, d->dta_seg, d->dta + i, i < (uint32_t)got ? buf[i] : 0);
		free(buf);
		status = (uint32_t)got == bytes ? 0 : !writing && got % size ? 3 : 1;
		if (writing) {
			uint32_t end = record * size + got;
			uint32_t length = rd16(c, seg, off + 0x10) | ((uint32_t)rd16(c, seg, off + 0x12) << 16);
			if (end > length) { wr16(c, seg, off + 0x10, end); wr16(c, seg, off + 0x12, end >> 16); }
		}
		uint32_t next = !block && !sequential ? record : record + records;
		wr16(c, seg, off + 0x0C, next / 128); wr8(c, seg, off + 0x20, next % 128);
		if (block) {
			c->r[CX] = records; wr16(c, seg, off + 0x21, next); wr16(c, seg, off + 0x23, next >> 16);
		}
	}
done:
	c->r[AX] = (c->r[AX] & 0xFF00) | status;
	return CPU_OK;
}

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
			int ch = con_peek(d);
			if (ch < 0) { c->flags |= FLAG_ZF; c->r[AX] &= 0xFF00; }
			else { c->flags &= ~FLAG_ZF; c->r[AX] = (c->r[AX] & 0xFF00) | con_getc(d); }
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
		c->r[AX] = (uint16_t)((c->r[AX] & 0xFF00) | (con_peek(d) >= 0 ? 0xFF : 0));
		return CPU_OK;

	case 0x0C: { /* flush the input buffer, then do AL's function */
		while (con_peek(d) >= 0) (void)con_getc(d);
		uint8_t al = (uint8_t)(c->r[AX] & 0xFF);
		c->r[AX] = (uint16_t)al << 8;
		if (al == 0x01 || al == 0x06 || al == 0x07 || al == 0x08 || al == 0x0A) {
			return dos21(d, c);
		}
		return CPU_OK;
	}

	case 0x0D: /* disk reset: nothing to do */
		return CPU_OK;

	case 0x0E: /* select the default drive: AL = 0 for A:, ... */
		if (dos_set_drive((int)((uint8_t)(c->r[AX] & 0xFF))) < 0) {
			c->r[AX] = 0x000F; /* invalid drive */
			return CPU_OK;
		}
		c->r[AX] = (uint16_t)((c->r[AX] & 0xFF00) | 0x1F); /* 32 drives */
		return CPU_OK;

	case 0x0F: case 0x10: case 0x14: case 0x15: case 0x16:
	case 0x21: case 0x22: case 0x24: case 0x27: case 0x28:
		return dos_fcb(d, c, ah);

	case 0x19: /* the current drive: AL = 0 for A:, 2 for C:, ... */
		c->r[AX] = (uint16_t)((c->r[AX] & 0xFF00) | (uint16_t)dos_get_drive());
		return CPU_OK;

	case 0x1A: /* set the disk transfer area */
		d->dta = dx;
		d->dta_seg = ds;
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
		c->sreg[0] = d->dta_seg;
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

	case 0x45: case 0x46: { /* duplicate / force duplicate file handle */
		if (bx >= HANDLES || d->handle_fd[bx] < 0) {
			c->r[AX] = 6; set_cf(c, 1); return CPU_OK;
		}
		int target = ah == 0x46 ? c->r[CX] : 5;
		if (ah == 0x45) while (target < HANDLES && d->handle_fd[target] >= 0) target++;
		if (target >= HANDLES) { c->r[AX] = 4; set_cf(c, 1); return CPU_OK; }
		if (target != bx) {
			int fd = dup(d->handle_fd[bx]);
			if (fd < 0) { c->r[AX] = 4; set_cf(c, 1); return CPU_OK; }
			if (d->handle_fd[target] > 2) close(d->handle_fd[target]);
			d->handle_fd[target] = fd;
		}
		c->r[AX] = target; set_cf(c, 0); return CPU_OK;
	}
	case 0x48: case 0x49: case 0x4A:
		return dos_memory(d, c, ah);

	case 0x47: { /* current directory: drive roots, without a leading slash */
		int drive = (dx & 255) ? (dx & 255) - 1 : dos_get_drive();
		char check[8] = { (char)('A' + drive), ':', '/', 0 };
		char mapped[256]; snprintf(mapped, sizeof(mapped), "%s", check);
		if (drive >= 26 || dos_path(mapped, sizeof(mapped)) < 0) {
			c->r[AX] = 15; set_cf(c, 1); return CPU_OK;
		}
		wr8(c, ds, c->r[SI], 0); set_cf(c, 0); return CPU_OK;
	}
	case 0x71: /* no Windows long-filename API; callers can use DOS 8.3 */
		c->r[AX] = 0x7100; set_cf(c, 1); return CPU_OK;

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

/* Case-insensitive wildcard matching for DOS directory searches. DOS's
 * conventional *.* also matches extensionless names. */
static int dos_match(const char *pattern, const char *name)
{
	if (!strcmp(pattern, "*.*")) pattern = "*";
	const char *star = NULL, *retry = NULL;
	while (*name) {
		if (*pattern == '?' || toupper((unsigned char)*pattern) == toupper((unsigned char)*name)) { pattern++; name++; }
		else if (*pattern == '*') { star = pattern++; retry = name; }
		else if (star) { pattern = star + 1; name = ++retry; }
		else return 0;
	}
	while (*pattern == '*') pattern++;
	return !*pattern;
}

static enum cpu_stop dos_find(struct dos_state *d, struct cpu *c, uint8_t ah)
{
	int slot = -1;
	for (int i = 0; i < 8; i++)
		if (d->find[i].dir && d->find[i].seg == d->dta_seg && d->find[i].off == d->dta) slot = i;
	if (ah == 0x4E) {
		char path[256];
		if (path_at(d, c->sreg[3], c->r[DX], path, sizeof(path)) < 0) goto missing;
		if (slot >= 0) { closedir(d->find[slot].dir); d->find[slot].dir = NULL; }
		else for (int i = 0; i < 8; i++) if (!d->find[i].dir) { slot = i; break; }
		if (slot < 0) goto missing;
		char *slash = strrchr(path, '/');
		if (!slash) goto missing;
		if (strlen(slash + 1) >= sizeof(d->find[slot].pattern)) goto missing;
		snprintf(d->find[slot].pattern, sizeof(d->find[slot].pattern), "%s", slash + 1);
		*slash = 0;
		snprintf(d->find[slot].root, sizeof(d->find[slot].root), "%s", path[0] ? path : "/");
		d->find[slot].dir = opendir(d->find[slot].root);
		if (!d->find[slot].dir) goto missing;
		d->find[slot].seg = d->dta_seg; d->find[slot].off = d->dta; d->find[slot].attr = c->r[CX];
	}
	if (slot < 0) goto missing;
	struct dirent *entry;
	while ((entry = readdir(d->find[slot].dir))) {
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
		if (strlen(entry->d_name) > 12 || !dos_match(d->find[slot].pattern, entry->d_name)) continue;
		char path[512]; struct stat st;
		snprintf(path, sizeof(path), "%s/%s", d->find[slot].root, entry->d_name);
		if (stat(path, &st) < 0) continue;
		uint8_t attr = S_ISDIR(st.st_mode) ? 0x10 : 0x20;
		if (attr == 0x10 && !(d->find[slot].attr & 0x10)) continue;
		uint16_t seg = d->dta_seg, off = d->dta;
		for (int i = 0; i < 43; i++) wr8(c, seg, off + i, 0);
		wr8(c, seg, off + 0x15, attr);
		wr16(c, seg, off + 0x18, 0x21); /* 1980-01-01: ManiOS has no file times */
		wr16(c, seg, off + 0x1A, (uint32_t)st.st_size);
		wr16(c, seg, off + 0x1C, (uint32_t)st.st_size >> 16);
		for (unsigned i = 0; entry->d_name[i]; i++) wr8(c, seg, off + 0x1E + i, toupper((unsigned char)entry->d_name[i]));
		set_cf(c, 0); c->r[AX] = 0; return CPU_OK;
	}
	closedir(d->find[slot].dir); d->find[slot].dir = NULL;
missing:
	c->r[AX] = ah == 0x4E ? 2 : 18; set_cf(c, 1); return CPU_OK;
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
		/* O_CREAT: DOS creates the file when it isn't there. The mode
		 * is 0644 rather than open's usual 0666 & ~umask, so a
		 * program's files are not left world-writable. */
		fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, 0644);
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
	case 0x4E: case 0x4F:
		return dos_find(d, c, ah);
	}
	set_cf(c, 1);
	c->r[AX] = 0x0002; /* file not found */
	return CPU_OK;
}

/* --- extended memory (XMS) --- */

static enum cpu_stop dos_xms(struct dos_state *d, struct cpu *c);

/* The XMS driver, INT 2Fh AH=43h: the entry point's address comes back
 * in ES:BX, and the program calls it far. dosrun answers the entry
 * point with a stub that sets AL=11h and does an INT 2Fh, which
 * dos_xms() below serves -- the way a real driver works, without a
 * separate code segment to build. */
static void xms_entry(struct dos_state *d, struct cpu *c)
{
	/* A far call to a stub: mov al,11h; int 2Fh; retf. The XMS
	 * function is already in AH. It goes near the top of the PSP's
	 * segment, past where a .COM's code and data reach. */
	uint16_t seg = d->psp_seg;
	uint16_t off = 0xFE00;
	wr8(c, seg, off, 0xB0);      /* mov al,11h */
	wr8(c, seg, (uint16_t)(off + 1), 0x11);
	wr8(c, seg, (uint16_t)(off + 2), 0xCD); /* INT */
	wr8(c, seg, (uint16_t)(off + 3), 0x2F);
	wr8(c, seg, (uint16_t)(off + 4), 0xCB); /* RETF */
	c->sreg[0] = seg;
	c->r[BX] = off;
}

/* INT 2Fh AH=43h: the XMS driver's installation check and entry point.
 * AL=00h is the check; AL=10h asks for the entry point; AL=11h is the
 * entry point itself (the stub above), which runs the XMS function in
 * AH. */
static enum cpu_stop int2f_xms(struct dos_state *d, struct cpu *c)
{
	uint8_t al = (uint8_t)(c->r[AX] & 0xFF);
	if (al == 0x00) { /* the installation check */
		c->r[AX] = (uint16_t)((c->r[AX] & 0xFF00) | (d->xms_installed ? 0x80 : 0x00));
		return CPU_OK;
	}
	if (al == 0x10) { /* the entry point: ES:BX */
		xms_entry(d, c);
		return CPU_OK;
	}
	if (al == 0x11) { /* through the entry point: the XMS function is AH */
		return dos_xms(d, c);
	}
	c->r[AX] = (uint16_t)(c->r[AX] & 0xFF00);
	return CPU_OK;
}

/* The XMS function dispatcher, entered through the stub with AH the
 * function. Returns CPU_OK; the program reads AX for the result. */
static enum cpu_stop dos_xms(struct dos_state *d, struct cpu *c)
{
	uint8_t ah = (uint8_t)(c->r[AX] >> 8);
	switch (ah) {
	case 0x00: /* get the version: 3.00 */
		c->r[AX] = 0x0300;
		c->r[BX] = 0x0001; /* the driver's internal revision */
		c->r[DX] = 0x0001; /* HMA exists */
		return CPU_OK;
	case 0x08: { /* query free extended memory: KB free, largest block */
		uint32_t free_kb = d->xms_size / 1024;
		c->r[AX] = (uint16_t)free_kb;
		c->r[DX] = (uint16_t)free_kb;
		return CPU_OK;
	}
	case 0x09: { /* allocate: DX = KB wanted, DX = the handle */
		uint32_t kb = c->r[DX];
		uint32_t bytes = kb * 1024;
		for (int i = 1; i < XMS_HANDLES; i++) {
			if (d->xms_handle[i] == 0) {
				/* A simple bump: the handle is the byte offset. */
				uint32_t used = 0;
				for (int j = 1; j < XMS_HANDLES; j++) {
					if (d->xms_handle[j] && d->xms_handle[j] + d->xms_len[j] > used) {
						used = d->xms_handle[j] + d->xms_len[j];
					}
				}
				if (bytes == 0 || used + bytes > d->xms_size) {
					c->r[AX] = 0x0000; /* out of memory */
					c->r[DX] = 0x0000;
					return CPU_OK;
				}
				d->xms_handle[i] = used + 1; /* never 0 */
				d->xms_len[i] = bytes;
				c->r[AX] = 0x0001;
				c->r[DX] = (uint16_t)i;
				return CPU_OK;
			}
		}
		c->r[AX] = 0x0000;
		c->r[DX] = 0x0000;
		return CPU_OK;
	}
	case 0x0A: { /* free: DX = the handle */
		uint16_t h = c->r[DX];
		if (h < XMS_HANDLES && d->xms_handle[h]) {
			d->xms_handle[h] = 0;
			d->xms_len[h] = 0;
			c->r[AX] = 0x0001;
		} else {
			c->r[AX] = 0x0000;
		}
		return CPU_OK;
	}
	case 0x0B: { /* move: DS:SI the descriptor, DX the byte count */
		/* The descriptor, as the XMS spec lays it out:
		 *   +0  DWORD count
		 *   +4  WORD  source handle
		 *   +6  DWORD source offset (low word the offset, high the
		 *             segment; a handle of 0 means the program's own
		 *             memory)
		 *   +10 WORD  dest handle
		 *   +12 DWORD dest offset
		 */
		uint16_t ds = c->sreg[3];
		uint16_t src_h = rd16(c, ds, (uint16_t)(c->r[SI] + 4));
		uint16_t src_off = rd16(c, ds, (uint16_t)(c->r[SI] + 6));
		uint16_t src_seg = rd16(c, ds, (uint16_t)(c->r[SI] + 8));
		uint16_t dst_h = rd16(c, ds, (uint16_t)(c->r[SI] + 10));
		uint16_t dst_off = rd16(c, ds, (uint16_t)(c->r[SI] + 12));
		uint16_t dst_seg = rd16(c, ds, (uint16_t)(c->r[SI] + 14));
		/* The source and destination, as byte pointers. */
		uint8_t *src = src_h == 0 ? c->mem + lin(src_seg, src_off)
		                          : d->xms + d->xms_handle[src_h] - 1 + src_off;
		uint8_t *dst = dst_h == 0 ? c->mem + lin(dst_seg, dst_off)
		                          : d->xms + d->xms_handle[dst_h] - 1 + dst_off;
		uint32_t n = c->r[DX];
		if (n > 0x10000) {
			n = 0x10000; /* one move is at most 64 KiB */
		}
		memmove(dst, src, n);
		c->r[AX] = 0x0001;
		c->r[DX] = (uint16_t)n;
		return CPU_OK;
	}
	case 0x0C: { /* lock: DX = the handle (nothing to lock here) */
		c->r[AX] = 0x0001;
		c->r[DX] = 0x0000;
		return CPU_OK;
	}
	case 0x0D: { /* unlock */
		c->r[AX] = 0x0001;
		return CPU_OK;
	}
	case 0x0E: { /* get the handle's information: BH = the size in KB */
		uint16_t h = c->r[DX];
		if (h < XMS_HANDLES && d->xms_handle[h]) {
			c->r[AX] = 0x0001;
			c->r[BX] = (uint16_t)(d->xms_len[h] / 1024);
			c->r[DX] = 0x0001; /* one lock */
		} else {
			c->r[AX] = 0x0000;
		}
		return CPU_OK;
	}
	case 0x0F: { /* reallocate: BX = the new size in KB */
		uint16_t h = c->r[DX];
		uint32_t kb = c->r[BX];
		if (h < XMS_HANDLES && d->xms_handle[h] && kb * 1024 <= d->xms_size) {
			d->xms_len[h] = kb * 1024;
			c->r[AX] = 0x0001;
		} else {
			c->r[AX] = 0x0000;
		}
		return CPU_OK;
	}
	default:
		c->r[AX] = 0x0000;
		return CPU_OK;
	}
}

/* --- expanded memory (EMS) --- */

/* INT 67h: the EMS driver. The functions a program uses to get a page
 * frame, allocate pages, and map them into the window. */
static enum cpu_stop int67(struct dos_state *d, struct cpu *c)
{
	uint8_t ah = (uint8_t)(c->r[AX] >> 8);
	if (!d->ems) {
		c->r[AX] = (uint16_t)((c->r[AX] & 0x00FF) | 0x8400); /* no driver */
		return CPU_OK;
	}
	switch (ah) {
	case 0x40: /* the status */
		c->r[AX] = (uint16_t)(c->r[AX] & 0x00FF); /* AH = 0: success */
		return CPU_OK;
	case 0x41: { /* get the page frame: BX the segment */
		c->r[BX] = d->ems_frame;
		c->r[AX] = (uint16_t)(c->r[AX] & 0x00FF); /* AH = 0: success */
		return CPU_OK;
	}
	case 0x42: { /* the unallocated page count: BX free, DX total */
		uint32_t used = 0;
		for (int i = 0; i < EMS_HANDLES; i++) {
			used += d->ems_pages_of[i];
		}
		c->r[BX] = (uint16_t)(d->ems_pages - used);
		c->r[DX] = (uint16_t)d->ems_pages;
		c->r[AX] = (uint16_t)(c->r[AX] & 0x00FF); /* AH = 0: success */
		return CPU_OK;
	}
	case 0x43: { /* allocate: BX the pages wanted, DX the handle */
		uint32_t want = c->r[BX];
		uint32_t used = 0;
		for (int i = 0; i < EMS_HANDLES; i++) {
			used += d->ems_pages_of[i];
		}
		if (want == 0 || used + want > d->ems_pages) {
			c->r[AX] = (uint16_t)((c->r[AX] & 0x00FF) | 0x8800); /* not enough */
			return CPU_OK;
		}
		for (int i = 0; i < EMS_HANDLES; i++) {
			if (d->ems_pages_of[i] == 0) {
				d->ems_handle[i] = 1;
				d->ems_pages_of[i] = want;
				c->r[DX] = (uint16_t)i;
				c->r[AX] = (uint16_t)(c->r[AX] & 0x00FF); /* AH = 0: success */
				return CPU_OK;
			}
		}
		c->r[AX] = (uint16_t)((c->r[AX] & 0x00FF) | 0x8500); /* no handles */
		return CPU_OK;
	}
	case 0x44: { /* map: AL the physical page, BX the logical, DX the handle */
		uint8_t phys = (uint8_t)(c->r[AX] & 0xFF);
		uint16_t h = c->r[DX];
		uint16_t logical = c->r[BX];
		if (h >= EMS_HANDLES || d->ems_pages_of[h] == 0 || logical >= d->ems_pages_of[h] || phys > 3) {
			c->r[AX] = (uint16_t)((c->r[AX] & 0x00FF) | 0x8A00); /* bad argument */
			return CPU_OK;
		}
		uint32_t at = lin(d->ems_frame, (uint16_t)(phys * EMS_PAGE));
		/* The frame is a window: write what it holds back to the page
		 * it was showing, then copy the new page in. */
		if (d->ems_map_h[phys] >= 0) {
			uint32_t old = 0;
			for (int i = 0; i < d->ems_map_h[phys]; i++) {
				old += d->ems_pages_of[i];
			}
			old += (uint32_t)d->ems_map_p[phys];
			uint8_t *back = d->ems + old * EMS_PAGE;
			for (uint32_t i = 0; i < EMS_PAGE; i++) {
				back[i] = c->mem[(at + i) & (MEM_SIZE - 1)];
			}
		}
		uint32_t page = 0;
		for (int i = 0; i < h; i++) {
			page += d->ems_pages_of[i];
		}
		page += logical;
		uint8_t *src = d->ems + page * EMS_PAGE;
		for (uint32_t i = 0; i < EMS_PAGE; i++) {
			c->mem[(at + i) & (MEM_SIZE - 1)] = src[i];
		}
		d->ems_map_h[phys] = h;
		d->ems_map_p[phys] = logical;
		c->r[AX] = (uint16_t)(c->r[AX] & 0x00FF); /* AH = 0: success */
		return CPU_OK;
	}
	case 0x45: { /* free: DX the handle */
		uint16_t h = c->r[DX];
		if (h < EMS_HANDLES && d->ems_pages_of[h]) {
			d->ems_pages_of[h] = 0;
			d->ems_handle[h] = 0;
			/* If the handle was in the window, the window is now
			 * empty. */
			for (int i = 0; i < 4; i++) {
				if (d->ems_map_h[i] == h) {
					d->ems_map_h[i] = -1;
					d->ems_map_p[i] = -1;
				}
			}
			c->r[AX] = (uint16_t)(c->r[AX] & 0x00FF); /* AH = 0: success */
		} else {
			c->r[AX] = (uint16_t)((c->r[AX] & 0x00FF) | 0x8300);
		}
		return CPU_OK;
	}
	case 0x47: /* save the mapping context: nothing to save */
		c->r[AX] = (uint16_t)(c->r[AX] & 0x00FF); /* AH = 0: success */
		return CPU_OK;
	case 0x48: /* restore the mapping context */
		c->r[AX] = (uint16_t)(c->r[AX] & 0x00FF); /* AH = 0: success */
		return CPU_OK;
	case 0x4B: { /* the handle count: BX the open handles */
		uint16_t n = 0;
		for (int i = 0; i < EMS_HANDLES; i++) {
			if (d->ems_pages_of[i]) {
				n++;
			}
		}
		c->r[BX] = n;
		c->r[AX] = (uint16_t)(c->r[AX] & 0x00FF); /* AH = 0: success */
		return CPU_OK;
	}
	case 0x4C: { /* the pages of a handle: BX the count */
		uint16_t h = c->r[DX];
		c->r[BX] = h < EMS_HANDLES ? (uint16_t)d->ems_pages_of[h] : 0;
		c->r[AX] = (uint16_t)(c->r[AX] & 0x00FF); /* AH = 0: success */
		return CPU_OK;
	}
	case 0x4D: { /* all the handles: ES:DI the array, BX the count */
		uint16_t n = 0;
		for (int i = 0; i < EMS_HANDLES; i++) {
			if (d->ems_pages_of[i]) {
				wr16(c, c->sreg[0], (uint16_t)(c->r[DI] + n * 2), (uint16_t)i);
				n++;
			}
		}
		c->r[BX] = n;
		c->r[AX] = (uint16_t)(c->r[AX] & 0x00FF); /* AH = 0: success */
		return CPU_OK;
	}
	case 0x51: case 0x52: case 0x53: { /* the page map: BX the entries */
		c->r[BX] = 4;
		c->r[AX] = (uint16_t)(c->r[AX] & 0x00FF); /* AH = 0: success */
		return CPU_OK;
	}
	case 0x58: { /* the hardware configuration: the frame's segment */
		wr16(c, c->sreg[0], c->r[DI], d->ems_frame);
		c->r[AX] = (uint16_t)(c->r[AX] & 0x00FF); /* AH = 0: success */
		return CPU_OK;
	}
	default:
		c->r[AX] = (uint16_t)((c->r[AX] & 0x00FF) | 0x8400); /* unsupported */
		return CPU_OK;
	}
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
/* Text pages mirror BIOS character/attribute cells in B800 memory. The
 * terminal receives ANSI positioning and colours for BIOS-driven rendering. */
static void text_position(struct dos_state *d, uint16_t pos)
{
	char seq[32]; int n = snprintf(seq, sizeof(seq), "\033[%u;%uH", (pos >> 8) + 1, (pos & 255) + 1);
	write(d->con_out, seq, n);
}

static void text_colour(struct dos_state *d, uint8_t attr)
{
	static const uint8_t ansi[8] = {0,4,2,6,1,5,3,7};
	char seq[32]; int n = snprintf(seq, sizeof(seq), "\033[%u;%um", (attr & 8 ? 90 : 30) + ansi[attr & 7], 40 + ansi[(attr >> 4) & 7]);
	write(d->con_out, seq, n);
}

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
	case 0x01: d->cursor_shape = c->r[CX]; return CPU_OK;
	case 0x02: {
		int page = (c->r[BX] >> 8) & 7;
		uint16_t pos = c->r[DX];
		if ((pos >> 8) < 25 && (pos & 255) < 80) {
			d->cursor[page] = pos;
			if (page == d->active_page) text_position(d, pos);
		}
		return CPU_OK;
	}
	case 0x03:
		c->r[DX] = d->cursor[(c->r[BX] >> 8) & 7]; c->r[CX] = d->cursor_shape; return CPU_OK;
	case 0x05: d->active_page = al & 7; text_position(d, d->cursor[d->active_page]); return CPU_OK;
	case 0x08: {
		int page = (c->r[BX] >> 8) & 7;
		uint16_t pos = d->cursor[page];
		c->r[AX] = mem_r16(c, 0xB8000 + page * 4096 + (((pos >> 8) * 80 + (pos & 255)) * 2));
		return CPU_OK;
	}
	case 0x09: case 0x0A: {
		int page = (c->r[BX] >> 8) & 7; uint16_t pos = d->cursor[page];
		unsigned start = (pos >> 8) * 80 + (pos & 255), n = c->r[CX];
		if (n > 2000 - start) n = 2000 - start;
		for (unsigned i = 0; i < n; i++) {
			uint32_t at = 0xB8000 + page * 4096 + (start + i) * 2;
			mem_w8(c, at, al);
			if (ah == 0x09) mem_w8(c, at + 1, c->r[BX] & 255);
		}
		if (page == d->active_page) {
			text_position(d, pos);
			if (ah == 0x09) text_colour(d, c->r[BX] & 255);
			for (unsigned i = 0; i < n; i++) write(d->con_out, &al, 1);
			write(d->con_out, "\033[0m", 4); text_position(d, pos);
		}
		return CPU_OK;
	}
	case 0x0E:
		con_putc(d, al); return CPU_OK;
	case 0x0F:
		c->r[AX] = (80 << 8) | d->video_mode;
		c->r[BX] = (c->r[BX] & 255) | (d->active_page << 8); return CPU_OK;
	case 0x06: case 0x07:
		return CPU_OK; /* scrolling remains to be implemented */
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
		c->r[AX] = key_word(ch);
		return CPU_OK;
	}
	case 0x01: case 0x11: /* key ready? ZF set if not */
		if (con_peek(d) < 0) { c->flags |= FLAG_ZF; c->r[AX] = 0; }
		else { c->flags &= ~FLAG_ZF; c->r[AX] = key_word(d->pending_key); }
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

static enum cpu_stop dispatch_int(struct dos_state *d, struct cpu *c, uint8_t vector)
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
	case 0x2F:
		return int2f_xms(d, c);
	case 0x67:
		return int67(d, c);
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

enum cpu_stop dos_int(struct dos_state *d, struct cpu *c, uint8_t vector)
{
	enum cpu_stop stop = dispatch_int(d, c, vector);
	if (stop == CPU_OK) {
		/* The caller returns through the INT frame. Updating only the
		 * live FLAGS register loses service results (CF for DOS errors,
		 * ZF for BIOS key polling) when IRET restores the saved word.
		 * Copy status flags into that word, preserving control flags:
		 * INT itself cleared IF and TF in the live register. */
		const uint16_t status = FLAG_CF | FLAG_PF | FLAG_AF |
		                        FLAG_ZF | FLAG_SF | FLAG_OF;
		uint16_t off = (uint16_t)(c->r[SP] + 4);
		uint16_t saved = rd16(c, c->sreg[2], off);
		wr16(c, c->sreg[2], off,
		     (uint16_t)((saved & ~status) | (c->flags & status)));
	}
	return stop;
}
