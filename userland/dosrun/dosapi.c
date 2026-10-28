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
#include "loader.h"

#include <errno.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#ifdef MANIOS_VERSION
#include <sys/filetime.h>
#else
#include <time.h>
/* Host harness adapter for the same persisted file metadata. */
static int filetime(int fd, uint32_t *stamp, int set)
{
	struct stat st;
	if (fstat(fd, &st) < 0) return -1;
	if (!set) {
		struct tm tm;
		if (!localtime_r(&st.st_mtime, &tm) || tm.tm_year < 80 || tm.tm_year > 207) {
			errno = EINVAL; return -1;
		}
		*stamp = (uint32_t)(((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday) << 16
		       | (tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2);
		return 0;
	}
	unsigned date = *stamp >> 16, t = *stamp & 0xFFFF;
	struct tm tm = {0};
	tm.tm_year = 80 + (date >> 9); tm.tm_mon = ((date >> 5) & 15) - 1;
	tm.tm_mday = date & 31; tm.tm_hour = t >> 11;
	tm.tm_min = (t >> 5) & 63; tm.tm_sec = (t & 31) * 2; tm.tm_isdst = -1;
	struct timespec times[2] = {st.st_atim, {mktime(&tm), 0}};
	return futimens(fd, times);
}

/* Host-side DOS API tests use Unix mode bits for the read-only flag; the
 * guest implementation below persists the complete supported FAT bits. */
static int fileattr(const char *path, uint8_t *attr, int set)
{
	struct stat st;
	if (stat(path, &st) < 0) return -1;
	if (set) {
		mode_t mode = st.st_mode;
		if (*attr & 1) mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
		else mode |= S_IWUSR;
		return chmod(path, mode);
	}
	*attr = (uint8_t)((S_ISDIR(st.st_mode) ? 0x10 : 0x20)
			  | ((st.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) ? 0 : 1));
	return 0;
}
#endif
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

/* The open files, one per DOS handle. Handles 0-4 initially refer to
 * the console; any closed slot can subsequently hold an opened file. */
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

struct dos_find {
	DIR *dir;
	uint16_t seg, off, attr;
	char root[256], pattern[128];
};
struct dos_parent {
	struct dos_parent *next;
	struct cpu cpu;
	uint16_t psp, prog, dta_seg, dta;
	int handles[HANDLES], fcb[16];
	uint8_t private_handles[HANDLES];
	uint8_t handle_drive[HANDLES];
	struct dos_find find[8];
};

struct dos_state {
	struct dos_parent *parent;
	uint16_t arena_seg;
	uint16_t child_status;
	struct cpu *cpu;
	uint32_t clock_fraction;
	/* Fixed PC master-PIC routing; IRQ0 is edge latched and held in service
	 * until EOI. Timer pulses continue while masked or IF is clear. */
	uint8_t pic1_mask, pic1_irr, pic1_isr, pic1_read_isr;
	/* PIT0 uses oscillator clocks supplied by dos_clock_advance. */
	struct {
		uint32_t phase, divisor;
		uint16_t latch;
		uint8_t low, write_high, read_high, latched, latch_high;
		uint8_t loaded, mode;
	} pit0;
	struct {
		uint32_t last_insns, phase, divisor;
		uint16_t latch;
		uint8_t low, write_high, read_high, latched, latch_high;
		uint8_t configured, loaded, control;
	} pit2;
	uint16_t psp_seg;
	uint16_t prog_seg;
	uint16_t dta, dta_seg; /* disk transfer area, independent of later DS */
	struct dos_find find[8];
	uint8_t exit_code;
	char executable[256]; /* DOS pathname in the environment trailer. */
	int con_in;        /* the console, for a program's reads */
	int con_out;
	int keyboard_fd;
	int vga_fd;        /* /dev/vga: the shared text screen's raw cells */
	int keyboard_alt_next;
	uint16_t keyboard_bda_head;
	uint16_t keyboard_bda_tail;
	uint16_t keyboard_bda_word;
	uint8_t keyboard_bda_pending;
	char last_console_line[128];
	uint8_t last_console_len;
	uint8_t last_console_line_ready;
	uint8_t keyboard_port_data[2];
	uint8_t keyboard_port_count;
	uint8_t keyboard_port_has_event;
	uint8_t keyboard_port_from_bios;
	uint8_t keyboard_port_reading;
	int pending_key;
	int pending_key2;
	int pending_bios_key;
	int pending_dos_scan;
	int fcb_fd[16];
	/* The video mode: 3 is text, 13h is 320x200 graphics. */
	int video_mode;
	uint16_t cursor[8], cursor_shape;
	uint8_t active_page;
	uint16_t text_seen[80 * 25];
	/* Mode 13h: ManiOS's screen, and whether it is open. */
	struct gfx_screen screen;
	int screen_open;
	/* The open files, one per DOS handle: 0-4 are the standard ones, a
	 * program's own files start at 5. */
	int handle_fd[HANDLES];
	uint8_t private_handles[HANDLES];
	uint8_t handle_drive[HANDLES];

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

/* Minimal colour-adapter Input Status Register 1. Synthetic scan timing is
 * driven by guest instructions, not by read count, so either-polarity polling
 * loops can finish even without interrupts. Bit 0 means display disabled
 * (horizontal or vertical blanking); bit 3 means vertical retrace. Other bits
 * stay zero. This is not a cycle-accurate or programmable CRTC. */
static int video_status_in8(struct cpu *c, uint16_t port, uint8_t *value)
{
	if (port != 0x03DA) return -1;
	uint32_t phase = c->insns & 4095u;
	int vertical = phase >= 3840u;
	int horizontal = (phase & 63u) >= 48u;
	*value = (uint8_t)((vertical ? 8 : 0) | ((vertical || horizontal) ? 1 : 0));
	return 0;
}

/* A bounded PIT channel-2 model: binary mode 3, low/high-byte programming
 * and count latch. Like video status, one guest instruction is a synthetic
 * input clock. No timer IRQs or native hardware access are involved. */
static void pit2_sync(struct dos_state *d)
{
	uint32_t now = d->cpu->insns, elapsed = now - d->pit2.last_insns;
	d->pit2.last_insns = now;
	if (d->pit2.loaded && (d->pit2.control & 1))
		d->pit2.phase = (uint32_t)(((uint64_t)d->pit2.phase + elapsed) % d->pit2.divisor);
}

static uint16_t pit2_count(struct dos_state *d)
{
	if (!d->pit2.loaded) return 0;
	uint32_t n = d->pit2.divisor, p = d->pit2.phase;
	if (p >= (n + 1) / 2) return (uint16_t)(2 * (n - p));
	return (uint16_t)(n + ((n & 1) && p ? 1 : 0) - 2 * p);
}

void dos_timer_advance(struct dos_state *d, uint64_t clocks)
{
	if (!d->pit0.loaded || !clocks) return;
	uint32_t n = d->pit0.divisor;
	if (d->pit0.mode == 0) {
		uint32_t left = n - d->pit0.phase;
		if (left && clocks >= left) d->pic1_irr |= 1;
		d->pit0.phase += clocks < left ? (uint32_t)clocks : left;
	} else {
		uint64_t total = d->pit0.phase + clocks;
		if (total >= n) d->pic1_irr |= 1;
		d->pit0.phase = (uint32_t)(total % n);
	}
}

static int dos_irq_ack(struct cpu *c)
{
	struct dos_state *d = c->port_context;
	if (!(d->pic1_irr & 1) || (d->pic1_mask & 1) || d->pic1_isr) return -1;
	d->pic1_irr &= (uint8_t)~1;
	d->pic1_isr |= 1;
	return 8;
}

static uint16_t pit0_count(struct dos_state *d)
{
	if (!d->pit0.loaded) return 0;
	uint32_t n = d->pit0.divisor, p = d->pit0.phase;
	if (d->pit0.mode == 3) {
		if (p >= (n + 1) / 2) return (uint16_t)(2 * (n - p));
		return (uint16_t)(n + ((n & 1) && p ? 1 : 0) - 2 * p);
	}
	return (uint16_t)(p < n ? n - p : 0);
}

static int pit0_command(struct dos_state *d, uint8_t value)
{
	/* Channel 0, low/high byte access, binary count. Modes 0, 2 and 3 are
	 * enough for system ticks, baud-rate generators and common delay loops. */
	if (value == 0x00) {
		if (!d->pit0.latched) {
			d->pit0.latch = pit0_count(d);
			d->pit0.latched = 1;
			d->pit0.latch_high = 0;
		}
		return 0;
	}
	if (value != 0x30 && value != 0x34 && value != 0x36) return -1;
	d->pit0.mode = value == 0x30 ? 0 : value == 0x34 ? 2 : 3;
	d->pit0.phase = 0;
	d->pit0.loaded = 0;
	d->pit0.write_high = d->pit0.read_high = d->pit0.latched = 0;
	return 0;
}

static int pit0_out(struct dos_state *d, uint8_t value)
{
	if (!d->pit0.write_high) d->pit0.low = value;
	else {
		uint16_t reload = d->pit0.low | ((uint16_t)value << 8);
		d->pit0.divisor = reload ? reload : 65536u;
		d->pit0.phase = 0;
		d->pit0.loaded = 1;
	}
	d->pit0.write_high ^= 1;
	return 0;
}

static int pit0_in(struct dos_state *d, uint8_t *value)
{
	if (!d->pit0.loaded) return -1;
	uint16_t count = d->pit0.latched ? d->pit0.latch : pit0_count(d);
	uint8_t *high = d->pit0.latched ? &d->pit0.latch_high : &d->pit0.read_high;
	*value = (uint8_t)(*high ? count >> 8 : count);
	*high ^= 1;
	if (d->pit0.latched && !*high) d->pit0.latched = 0;
	return 0;
}

static int con_peek(struct dos_state *d);
static int con_get_rawc(struct dos_state *d);
static uint16_t key_word(int ch);

/* A few DOS editors bypass INT 16h and poll the AT keyboard controller.
 * Present the same queued key through ports 64h (status) and 60h (data). */
static void keyboard_port_fill(struct dos_state *d)
{
	if (d->keyboard_port_count) return;
	int key = con_peek(d);
	if (key < 0) return;
	uint8_t scan = key_word(key) >> 8;
	if (!scan) return;
	if (key > 0xFF) {
		d->keyboard_port_data[0] = 0xE0;
		d->keyboard_port_data[1] = scan;
		d->keyboard_port_count = 2;
	} else {
		d->keyboard_port_data[0] = scan;
		d->keyboard_port_count = 1;
	}
	d->keyboard_port_has_event = 1;
}

static int dos_port_in8(struct cpu *c, uint16_t port, uint8_t *value)
{
	struct dos_state *d = c->port_context;
	if (port == 0x64) {
		keyboard_port_fill(d);
		*value = d->keyboard_port_count ? 1 : 0;
		return 0;
	}
	if (port == 0x60) {
		keyboard_port_fill(d);
		if (!d->keyboard_port_count) { *value = 0; return 0; }
		*value = d->keyboard_port_data[0];
		if (d->keyboard_port_count == 2) d->keyboard_port_data[0] = d->keyboard_port_data[1];
		d->keyboard_port_count--;
		if (!d->keyboard_port_count && d->keyboard_port_has_event) {
			d->keyboard_port_reading = 1;
			(void)con_get_rawc(d);
			d->keyboard_port_reading = 0;
			d->keyboard_port_has_event = 0;
		}
		return 0;
	}
	if (port == 0x3DA) return video_status_in8(c, port, value);
	if (port == 0x20) { *value = d->pic1_read_isr ? d->pic1_isr : d->pic1_irr; return 0; }
	if (port == 0x21) { *value = d->pic1_mask; return 0; }
	if (port == 0x40) return pit0_in(d, value);
	if (port != 0x42 && port != 0x61) return -1;
	if (port == 0x42 && !d->pit2.configured) return -1;
	pit2_sync(d);
	if (port == 0x61) {
		int out = !d->pit2.loaded || !(d->pit2.control & 1) ||
		          d->pit2.phase < (d->pit2.divisor + 1) / 2;
		*value = (uint8_t)((d->pit2.control & 15) | (out ? 0x20 : 0));
		return 0;
	}
	uint16_t count = d->pit2.latched ? d->pit2.latch : pit2_count(d);
	uint8_t *high = d->pit2.latched ? &d->pit2.latch_high : &d->pit2.read_high;
	*value = (uint8_t)(*high ? count >> 8 : count);
	*high ^= 1;
	if (d->pit2.latched && !*high) d->pit2.latched = 0;
	return 0;
}

static int dos_port_out8(struct cpu *c, uint16_t port, uint8_t value)
{
	struct dos_state *d = c->port_context;
	/* CGA mode/color select writes do not switch the host text backend. DOS
	 * text programs sometimes touch these after using BIOS INT 10h; the BIOS
	 * text page remains the source of the rendered image in this runner. */
	if (port == 0x3D8 || port == 0x3D9) return 0;
	if (port == 0x20) {
		if (value == 0x20 || value == 0x60) { d->pic1_isr &= (uint8_t)~1; return 0; }
		if (value == 0x0A || value == 0x0B) { d->pic1_read_isr = value & 1; return 0; }
		return -1;
	}
	if (port == 0x21) { d->pic1_mask = value; return 0; }
	if (port == 0x40) return pit0_out(d, value);
	if (port == 0x43 && value <= 0x36 && (value == 0 || value == 0x30 || value == 0x34 || value == 0x36))
		return pit0_command(d, value);
	/* Reject commands before changing any device state. Other channels,
	 * modes, BCD and 8254 read-back are not implemented. */
	if (port == 0x43) {
		if (value != 0xB6 && value != 0x80) return -1;
		if (value == 0x80 && !d->pit2.configured) return -1;
	} else if (port == 0x42) {
		if (!d->pit2.configured) return -1;
	} else if (port != 0x61) return -1;
	pit2_sync(d);
	if (port == 0x43) {
		if (value == 0x80) {
			if (!d->pit2.latched) {
				d->pit2.latch = pit2_count(d); d->pit2.latched = 1; d->pit2.latch_high = 0;
			}
		} else {
			d->pit2.configured = 1; d->pit2.loaded = 0; d->pit2.phase = 0;
			d->pit2.write_high = d->pit2.read_high = d->pit2.latched = 0;
		}
	} else if (port == 0x42) {
		if (!d->pit2.write_high) d->pit2.low = value;
		else {
			uint16_t reload = d->pit2.low | ((uint16_t)value << 8);
			d->pit2.divisor = reload ? reload : 65536u;
			d->pit2.loaded = 1; d->pit2.phase = 0;
		}
		d->pit2.write_high ^= 1;
	} else {
		/* Low four bits are writable latches; bits 4..7 are read-only.
		 * Gate rising restarts the count; gate falling forces OUT2 high. */
		if (!(d->pit2.control & 1) && (value & 1)) d->pit2.phase = 0;
		d->pit2.control = value & 15;
	}
	return 0;
}

unsigned dos_speaker_frequency(struct dos_state *d)
{
	return d->pit2.loaded && (d->pit2.control & 3) == 3 ? 1193182u / d->pit2.divisor : 0;
}

/* The text screen is shared between DOS processes the way a PC's B800
 * memory is shared between programs: a process inherits the cells the
 * last one left and leaves its own behind. The cells live in a scratch
 * file on the root ramfs, so they exist for as long as the boot does
 * and nothing on the user's disks is touched. */
#define VGA_SHARE_BYTES (80 * 25 * 2 + 2)
static const char VGA_SHARE_PATH[] = "/dos-screen.bin";

static void video_screen_restore(struct dos_state *d)
{
#ifdef MANIOS_VERSION
	int fd = open(VGA_SHARE_PATH, O_RDWR | O_CREAT, 0644);
	if (fd < 0) return;
	uint8_t buf[VGA_SHARE_BYTES];
	long got = read(fd, buf, sizeof(buf));
	d->vga_fd = fd;
	if (got != VGA_SHARE_BYTES) {
		/* A fresh boot: the pages already hold the POST pattern, and the
		 * save at exit leaves the screen for the next process. */
		return;
	}
	for (unsigned cell = 0; cell < 80 * 25; cell++) {
		uint16_t v = (uint16_t)(buf[cell * 2] | buf[cell * 2 + 1] << 8);
		mem_w16(d->cpu, 0xB8000 + cell * 2, v);
		d->text_seen[cell] = v;
	}
	uint16_t linear = (uint16_t)(buf[VGA_SHARE_BYTES - 2] | buf[VGA_SHARE_BYTES - 1] << 8);
	if (linear < 80 * 25) {
		d->cursor[0] = (uint16_t)((linear / 80 << 8) | linear % 80);
		wr16(d->cpu, 0x40, 0x50, d->cursor[0]);
	}
#endif
}

static void video_screen_save(struct dos_state *d)
{
#ifdef MANIOS_VERSION
	if (d->vga_fd < 0) return;
	uint8_t buf[VGA_SHARE_BYTES];
	uint32_t base = 0xB8000 + d->active_page * 4096;
	for (unsigned cell = 0; cell < 80 * 25; cell++) {
		uint16_t v = mem_r16(d->cpu, base + cell * 2);
		buf[cell * 2] = (uint8_t)v;
		buf[cell * 2 + 1] = (uint8_t)(v >> 8);
	}
	uint16_t pos = d->cursor[d->active_page];
	uint16_t linear = (uint16_t)((pos >> 8) * 80 + (pos & 255));
	buf[VGA_SHARE_BYTES - 2] = (uint8_t)(linear & 255);
	buf[VGA_SHARE_BYTES - 1] = (uint8_t)(linear >> 8);
	if (lseek(d->vga_fd, 0, SEEK_SET) == 0) (void)write(d->vga_fd, buf, sizeof(buf));
#endif
}

struct dos_state *dos_new(struct cpu *c)
{
	struct dos_state *d = calloc(1, sizeof(*d));
	if (!d) {
		return NULL;
	}
	d->cpu = c;
	/* BIOS ROM stubs are ordinary guest code, so saved/chained vectors work.
	 * The private F1 trap advances the BDA once; INT 1Ch is caller-hookable. */
	static const uint8_t timer_rom[] = {0xCD,0xF1,0xCD,0x1C,0x50,0xB0,0x20,0xE6,0x20,0x58,0xCF};
	/* DOS's INT 21h vector must also work when a guest chains to it with
	 * a far call. INT F2 is the private host-service trap; IRET returns to
	 * the caller's original interrupt frame (or far-call frame). */
	static const uint8_t dos21_rom[] = {0xCD,0xF2,0xCF};
	/* BIOS services are callable both with INT and through Pascal's
	 * far-call-to-IVT convention. These guest stubs keep IRET semantics
	 * for that convention while INT F3 bridges to the BIOS implementation. */
	static const uint8_t bios_vectors[] = {0x10,0x11,0x12,0x14,0x15,0x16,0x17,0x1A,0x29};
	static const uint8_t bios_stub[] = {0xCD,0xF3,0xCF};
	for (unsigned i = 0; i < sizeof(bios_vectors); i++) {
		uint16_t off = (uint16_t)(0x140 + i * 3);
		memcpy(c->mem + 0xF0000 + off, bios_stub, sizeof(bios_stub));
		wr16(c, 0, bios_vectors[i] * 4, off);
		wr16(c, 0, bios_vectors[i] * 4 + 2, 0xF000);
	}
	memcpy(c->mem + 0xF0100, timer_rom, sizeof(timer_rom));
	memcpy(c->mem + 0xF0130, dos21_rom, sizeof(dos21_rom));
	mem_w8(c, 0xF0120, 0xCF);
	wr16(c,0,8*4,0x100); wr16(c,0,8*4+2,0xF000);
	wr16(c,0,0x1C*4,0x120); wr16(c,0,0x1C*4+2,0xF000);
	wr16(c,0,0x21*4,0x130); wr16(c,0,0x21*4+2,0xF000);
	c->irq_ack = dos_irq_ack;
	/* PC BIOS normally leaves IRQ0 (timer) and IRQ1 (keyboard) unmasked. */
	d->pic1_mask = 0xFC;
	/* BIOS programs channel 0 in mode 3 with a zero divisor (65536). */
	d->pit0.divisor = 65536u;
	d->pit0.loaded = 1;
	d->pit0.mode = 3;
	d->pending_key = -1;
	d->pending_key2 = -1;
	d->pending_bios_key = -1;
	d->pending_dos_scan = -1;
	/* Conventional RAM ends where VGA memory begins (A0000h). */
	wr16(c, 0x40, 0x13, 640);
	wr16(c, 0x40, 0x1A, 0x1E);
	wr16(c, 0x40, 0x1C, 0x1E);
	for (int i = 0; i < 16; i++) d->fcb_fd[i] = -1;
	d->con_in = 0;
	d->con_out = 1;
	d->keyboard_fd = -1;
	d->vga_fd = -1;
#ifdef MANIOS_VERSION
	/* Raw PS/2 events let DOS programs receive arrows and function keys;
	 * the serial console remains available for line-oriented input. */
	d->keyboard_fd = open("/dev/kbd", O_RDONLY);
#endif
	d->video_mode = 3;
	d->cursor_shape = 0x0607;
	/* BIOS programs inspect the video BDA directly for mode geometry and
	 * active-page state. Keep it consistent with the text services below. */
	wr8(c, 0x40, 0x49, 3);
	wr16(c, 0x40, 0x4A, 80);
	wr16(c, 0x40, 0x4C, 0x1000);
	wr16(c, 0x40, 0x4E, 0);
	wr8(c, 0x40, 0x62, 0);
	wr16(c, 0x40, 0x63, 0x03D4);
	wr8(c, 0x40, 0x84, 24);
	wr16(c, 0x40, 0x60, d->cursor_shape);
	/* BIOS POST leaves the text pages as spaces in attribute 7; a fresh
	 * page of zeros would paint character-only writers black on black. */
	for (int page = 0; page < 8; page++) {
		uint32_t base = 0xB8000 + page * 4096;
		for (unsigned cell = 0; cell < 80 * 25; cell++) mem_w16(c, base + cell * 2, 0x0720);
	}
	video_screen_restore(d);
	d->dta = PSP_DTA;
	for (int i = 0; i < HANDLES; i++) {
		d->handle_fd[i] = -1;
		d->handle_drive[i] = (uint8_t)dos_get_drive();
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
	c->port_context = d;
	c->port_in8 = dos_port_in8;
	c->port_out8 = dos_port_out8;
	return d;
}

void dos_free(struct dos_state *d)
{
	if (d->cpu->port_context == d) {
		d->cpu->irq_ack = NULL; d->cpu->port_in8 = NULL; d->cpu->port_out8 = NULL; d->cpu->port_context = NULL;
	}
	for (int i = 0; i < 16; i++) if (d->fcb_fd[i] >= 0) close(d->fcb_fd[i]);
	if (d->keyboard_fd >= 0) close(d->keyboard_fd);
	if (d->vga_fd >= 0) close(d->vga_fd);
	for (int i = 0; i < 8; i++) if (d->find[i].dir) closedir(d->find[i].dir);
	for (int i = 0; i < HANDLES; i++) if (d->handle_fd[i] > 2) close(d->handle_fd[i]);
	while (d->parent) {
		struct dos_parent *p = d->parent;
		d->parent = p->next;
		for (int i = 0; i < HANDLES; i++) if (p->handles[i] > 2) close(p->handles[i]);
		for (int i = 0; i < 16; i++) if (p->fcb[i] >= 0) close(p->fcb[i]);
		for (int i = 0; i < 8; i++) if (p->find[i].dir) closedir(p->find[i].dir);
		free(p);
	}
	free(d->xms);
	free(d->ems);
	free(d);
}

void dos_set_keyboard_fd(struct dos_state *d, int fd)
{
	if (d->keyboard_fd >= 0) close(d->keyboard_fd);
	d->keyboard_fd = fd;
}

void dos_set_program(struct dos_state *d, uint16_t psp_seg, uint16_t prog_seg)
{
	d->psp_seg = psp_seg;
	if (!d->arena_seg) d->arena_seg = psp_seg - 1;
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
static void build_psp(struct dos_state *d)
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
}

/* The environment variables --env added for this run, written into the
 * child's environment block by dos_build_psp and cleared by it. */
static char env_extra_name[ENV_EXTRA_MAX][ENV_NAME_MAX];
static char env_extra_value[ENV_EXTRA_MAX][ENV_VALUE_MAX];
static int env_extra_count;

void dos_set_env(const char *name, const char *value)
{
	if (env_extra_count >= ENV_EXTRA_MAX) return;
	snprintf(env_extra_name[env_extra_count], sizeof(env_extra_name[0]), "%s", name);
	snprintf(env_extra_value[env_extra_count], sizeof(env_extra_value[0]), "%s", value);
	env_extra_count++;
}

int dos_set_executable(struct dos_state *d, const char *host_path)
{
	return dos_guest_path(host_path, d->executable, sizeof(d->executable));
}

void dos_build_psp(struct dos_state *d)
{
	struct cpu *c = d->cpu;
	uint16_t psp = d->psp_seg;
	uint16_t end = rd16(c, psp, PSP_MEMSIZE);
	/* load_program records the bounded MZ allocation here. Callers
	 * that construct a synthetic program without the loader retain
	 * the traditional full-arena default. */
	if (end < psp + 0x10u || end > 0xA000) end = 0xA000;
	build_psp(d);
	wr16(c, psp, PSP_MEMSIZE, end);
	/* A real, terminated environment below the process allocation. The
	 * word after the double NUL counts executable-name strings. The
	 * block must stay under the gap to the program's segment (0x100),
	 * so the extra variables stop writing well before the end. */
	const uint16_t env = 0x0F00;
	uint16_t off = 0;
	const char *vars[] = { "PATH=C:\\", "COMSPEC=C:\\COMMAND.COM" };
	for (unsigned i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
		for (const char *p = vars[i]; *p; p++) wr8(c, env, off++, *p);
		wr8(c, env, off++, 0);
	}
	for (int i = 0; i < env_extra_count && off < ENV_EXTRA_LIMIT; i++) {
		for (const char *p = env_extra_name[i]; *p && off < ENV_EXTRA_LIMIT; p++)
			wr8(c, env, off++, *p);
		wr8(c, env, off++, '=');
		for (const char *p = env_extra_value[i]; *p && off < ENV_EXTRA_LIMIT; p++)
			wr8(c, env, off++, *p);
		wr8(c, env, off++, 0);
	}
	wr8(c, env, off++, 0);
	wr16(c, env, off, 1);
	const char *program = d->executable[0] ? d->executable : "C:\\DOSRUN.EXE";
	for (unsigned i = 0; i <= strlen(program); i++) wr8(c, env, off + 2 + i, program[i]);
	wr16(c, psp, PSP_ENVIRON, env);
	/* The initial process and the free remainder form one valid MCB
	 * chain, just like allocations made through DOS AH=48h. */
	wr8(c, psp - 1, 0, end < 0xA000 ? 'M' : 'Z');
	wr16(c, psp - 1, 1, psp);
	wr16(c, psp - 1, 3, end - psp);
	if (end < 0xA000) {
		wr8(c, end, 0, 'Z');
		wr16(c, end, 1, 0);
		wr16(c, end, 3, 0xA000 - end - 1);
	}
	/* INT 21h/AH=52h returns ES:BX into this DOS list-of-lists area;
	 * the first MCB segment is stored two bytes before the returned pointer. */
	wr16(c, 0x0E00, 0, d->arena_seg);
	env_extra_count = 0;
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

static void text_scroll(struct dos_state *d, int page, int top, int left,
			int bottom, int right, int lines, int down, uint8_t attr);
static int con_getc(struct dos_state *d);
static void keyboard_bda_sync(struct dos_state *d);
static void keyboard_bda_consume(struct dos_state *d);
static int con_get_rawc(struct dos_state *d);

/* DOS writes CR and LF independently. A carriage return moves to column
 * zero without adding a line; the following LF advances the row. At the
 * bottom row, output scrolls the active text page instead of overwriting
 * the last line. */
static void con_putc(struct dos_state *d, uint8_t ch)
{
	uint16_t pos = d->cursor[d->active_page];
	unsigned row = pos >> 8, col = pos & 255;
	if (ch == '\r') col = 0;
	else if (ch == '\n') {
		if (row < 24) row++;
		else {
			d->cursor[d->active_page] = (row << 8) | col;
			text_scroll(d, d->active_page, 0, 0, 24, 79, 1, 0, 0x07);
			return;
		}
	}
	else if (ch == '\b') { if (col) col--; }
	else if (ch >= 32) {
		mem_w16(d->cpu, 0xB8000 + d->active_page * 4096 + (row * 80 + col) * 2, ch | 0x0700);
		d->text_seen[row * 80 + col] = ch | 0x0700;
		if (++col >= 80) {
			col = 0;
			if (row < 24) row++;
			else {
				d->cursor[d->active_page] = (row << 8) | col;
				text_scroll(d, d->active_page, 0, 0, 24, 79, 1, 0, 0x07);
				return;
			}
		}
	}
	d->cursor[d->active_page] = (row << 8) | col;
	write(d->con_out, &ch, 1);
}

static uint16_t key_word(int ch);

/* Translate the input driver's key stream to PC BIOS key words. */
static int keyboard_event(struct dos_state *d, uint8_t key)
{
	if (key == 0xA0) { /* ZKT_KEY_ALT prefixes the next key. */
		d->keyboard_alt_next = 1;
		return -2; /* modifier only; wait for its key */
	}
	int alt = d->keyboard_alt_next;
	d->keyboard_alt_next = 0;
	if (alt) {
		int scan = 0;
		if (key >= 0x90 && key <= 0x99) scan = 0x3B + key - 0x90;
		else scan = key_word(key) >> 8;
		if (scan) return 0x100 | scan; /* AL=0, AH=scan code */
	}
	switch (key) {
	case 0x80: return 0x148; /* Up */
	case 0x81: return 0x150; /* Down */
	case 0x82: return 0x14B; /* Left */
	case 0x83: return 0x14D; /* Right */
	case 0x84: return 0x147; /* Home */
	case 0x85: return 0x14F; /* End */
	case 0x86: return 0x149; /* Page Up */
	case 0x87: return 0x151; /* Page Down */
	case 0x88: return 0x152; /* Insert */
	case 0x89: return 0x153; /* Delete */
	default:
		if (key >= 0x90 && key <= 0x99) return 0x13B + key - 0x90;
		return key;
	}
}

/* Prefer raw keyboard events when available; serial input remains cooked by
 * the shell's line discipline and is useful for scripts and remote sessions. */
static int con_read_input(struct dos_state *d, int timeout_ms)
{
#ifdef MANIOS_VERSION
	struct zkt_pollfd fds[2];
	int keyboard_index = -1, count = 0;
	if (d->keyboard_fd >= 0) {
		keyboard_index = count;
		fds[count++] = (struct zkt_pollfd){ d->keyboard_fd, 0 };
	}
	fds[count++] = (struct zkt_pollfd){ d->con_in, 0 };
	if (poll(fds, count, timeout_ms) <= 0) return -1;
	int fd = keyboard_index >= 0 && fds[keyboard_index].ready
	       ? d->keyboard_fd : d->con_in;
	bool keyboard = keyboard_index >= 0 && fd == d->keyboard_fd;
#else
	struct pollfd fds[2];
	int keyboard_index = -1, count = 0;
	if (d->keyboard_fd >= 0) {
		keyboard_index = count;
		fds[count++] = (struct pollfd){ d->keyboard_fd, POLLIN, 0 };
	}
	fds[count++] = (struct pollfd){ d->con_in, POLLIN, 0 };
	if (poll(fds, count, timeout_ms) <= 0) return -1;
	int fd = keyboard_index >= 0 && (fds[keyboard_index].revents & POLLIN)
	       ? d->keyboard_fd : d->con_in;
	bool keyboard = keyboard_index >= 0 && fd == d->keyboard_fd;
#endif
	uint8_t byte;
	if (read(fd, &byte, 1) != 1) return -1;
	int key = keyboard ? keyboard_event(d, byte) : (byte == '\n' ? '\r' : byte);
	if (key == '\r' || key == '\n') {
		d->last_console_line[d->last_console_len] = '\0';
		d->last_console_line_ready = d->last_console_len != 0;
		d->last_console_len = 0;
	} else if (key == '\b') {
		if (d->last_console_len) d->last_console_len--;
		d->last_console_line_ready = 0;
	} else if (key >= 32 && key < 127) {
		d->last_console_line_ready = 0;
		if ((size_t)d->last_console_len + 1 < sizeof(d->last_console_line))
			d->last_console_line[d->last_console_len++] = (char)key;
	}
	return key == -2 ? con_read_input(d, timeout_ms) : key;
}

/* Cache a polled byte: BIOS readiness queries must not consume it. */
static int con_peek(struct dos_state *d)
{
	keyboard_bda_sync(d);
	if (d->pending_dos_scan >= 0) return d->pending_dos_scan;
	if (d->pending_key >= 0) return d->pending_key;
	int ch = con_read_input(d, 0);
	if (ch < 0) return -1;
	d->pending_key = ch;
	return ch;
}

static uint16_t key_word(int ch)
{
	static const uint8_t letters[] = {30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44};
	if (ch > 0xFF) return (uint16_t)((ch & 0xFF) << 8);
	int lower = tolower((unsigned char)ch), scan = 0;
	if (ch >= 1 && ch <= 26) scan = letters[ch - 1]; /* Ctrl+A..Z retain their key scan. */
	else if (lower >= 'a' && lower <= 'z') scan = letters[lower - 'a'];
	else if (ch >= '1' && ch <= '9') scan = ch - '1' + 2;
	else switch (ch) {
	case '0': scan = 11; break; case 27: scan = 1; break;
	case '\r': case '\n': scan = 28; ch = '\r'; break;
	case '\b': scan = 14; break; case '\t': scan = 15; break;
	case ' ': scan = 57; break;
	}
	return (scan << 8) | (uint8_t)ch;
}

static void keyboard_bda_sync(struct dos_state *d)
{
	if (!d->keyboard_bda_pending) return;
	uint16_t head = rd16(d->cpu, 0x40, 0x1A);
	if (head == d->keyboard_bda_head) return;
	/* A DOS program consumed the mirrored BIOS keyboard-buffer word directly. */
	if (d->pending_key == d->keyboard_bda_word) d->pending_key = -1;
	d->keyboard_bda_pending = 0;
}

static void keyboard_bda_consume(struct dos_state *d)
{
	keyboard_bda_sync(d);
	if (!d->keyboard_bda_pending) return;
	uint16_t head = d->keyboard_bda_head + 2;
	if (head >= 0x3E) head = 0x1E;
	wr16(d->cpu, 0x40, 0x1A, head);
	d->keyboard_bda_head = head;
	d->keyboard_bda_pending = 0;
}

void dos_keyboard_poll(struct dos_state *d)
{
	keyboard_bda_sync(d);
	if (d->pending_key >= 0 || d->pending_bios_key >= 0 || d->pending_dos_scan >= 0)
		return;
	int key = con_read_input(d, 0);
	if (key < 0) return;
	d->pending_key = key;
	uint16_t head = rd16(d->cpu, 0x40, 0x1A);
	uint16_t tail = rd16(d->cpu, 0x40, 0x1C);
	if (head != tail) return;
	uint16_t word = key_word(key), next = tail + 2;
	if (next >= 0x3E) next = 0x1E;
	wr16(d->cpu, 0x40, tail, word);
	wr16(d->cpu, 0x40, 0x1C, next);
	d->keyboard_bda_head = head;
	d->keyboard_bda_tail = next;
	d->keyboard_bda_word = key;
	d->keyboard_bda_pending = 1;
}

/* Serial terminals commonly encode cursor keys as CSI sequences. Decode
 * those for DOS programs when the terminal preserves ESC; physical PS/2
 * input is mapped directly by the keyboard device. */
static int bios_key_sequence(struct dos_state *d, int first)
{
	if (first != 27) return first;
#ifdef MANIOS_VERSION
	struct zkt_pollfd zfd = { d->con_in, 0 };
	if (poll(&zfd, 1, 25) <= 0 || !zfd.ready) return 27;
#else
	struct pollfd fd = { d->con_in, POLLIN, 0 };
	if (poll(&fd, 1, 25) <= 0 || !(fd.revents & POLLIN)) return 27;
#endif
	uint8_t second;
	if (read(d->con_in, &second, 1) != 1) return 27;
	if (second != '[') {
		d->pending_key = second;
		return 27;
	}
#ifdef MANIOS_VERSION
	if (poll(&zfd, 1, 25) <= 0 || !zfd.ready) {
#else
	fd.revents = 0;
	if (poll(&fd, 1, 25) <= 0 || !(fd.revents & POLLIN)) {
#endif
		d->pending_key = '[';
		return 27;
	}
	uint8_t third;
	if (read(d->con_in, &third, 1) != 1) {
		d->pending_key = '[';
		return 27;
	}
	int scan;
	switch (third) {
	case 'A': scan = 0x48; break; /* Up */
	case 'B': scan = 0x50; break; /* Down */
	case 'C': scan = 0x4D; break; /* Right */
	case 'D': scan = 0x4B; break; /* Left */
	default:
		d->pending_key = '[';
		d->pending_key2 = third;
		return 27;
	}
	return 0x100 | scan;
}

static int bios_key_peek(struct dos_state *d)
{
	if (d->pending_bios_key >= 0) return d->pending_bios_key;
	int ch = con_peek(d);
	if (ch == 27) {
		ch = con_get_rawc(d);
		if (ch < 0) return -1;
		d->pending_bios_key = bios_key_sequence(d, ch);
		return d->pending_bios_key;
	}
	return ch;
}

static int bios_key_get(struct dos_state *d)
{
	if (d->pending_bios_key >= 0) {
		int ch = d->pending_bios_key;
		d->pending_bios_key = -1;
		keyboard_bda_consume(d);
		return ch;
	}
	return bios_key_sequence(d, con_get_rawc(d));
}

/* Reads one key, waiting. Returns -1 at end of input. */
static int con_get_rawc(struct dos_state *d)
{
	if (d->pending_key >= 0) {
		int ch = d->pending_key;
		d->pending_key = d->pending_key2;
		d->pending_key2 = -1;
		keyboard_bda_consume(d);
		if (!d->keyboard_port_reading) {
			d->keyboard_port_count = 0;
			d->keyboard_port_has_event = 0;
		}
		return ch == '\n' ? '\r' : ch;
	}
	int ch = con_read_input(d, -1);
	if (!d->keyboard_port_reading) {
		d->keyboard_port_count = 0;
		d->keyboard_port_has_event = 0;
	}
	return ch == '\n' ? '\r' : ch;
}

/* DOS character reads represent extended keys as a zero byte followed by
 * the BIOS scan code. ANSI arrows are decoded at this shared console layer so
 * both BIOS INT 16h clients and DOS INT 21h clients can navigate. */
static int con_getc(struct dos_state *d)
{
	if (d->pending_dos_scan >= 0) {
		int scan = d->pending_dos_scan;
		d->pending_dos_scan = -1;
		return scan;
	}
	int ch = con_get_rawc(d);
	if (ch > 0xFF) {
		d->pending_dos_scan = ch & 0xFF;
		return 0;
	}
	if (ch == 27) {
		int key = bios_key_sequence(d, ch);
		if (key > 0xFF) {
			d->pending_dos_scan = key & 0xFF;
			return 0;
		}
		return key;
	}
	return ch;
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
	for (uint32_t at = d->arena_seg; at < 0xA000;) {
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

/* EXEC keeps suspended processes out of the child's handle/search tables.
 * Guest memory is shared, but each process owns separate MCBs and a PSP. */
static uint16_t exec_alloc(struct dos_state *d, uint16_t min, uint16_t max)
{
	struct cpu *c = d->cpu;
	for (uint32_t at = d->arena_seg; at < 0xA000;) {
		uint16_t m = at;
		if (!mcb_valid(c, m)) return 0;
		if (!rd16(c, m, 1)) {
			mcb_join(c, m);
			uint16_t size = rd16(c, m, 3);
			if (size >= min) {
				mcb_split(c, m, size < max ? size : max);
				wr16(c, m, 1, 0xFFFF);
				return m + 1;
			}
		}
		if (rd8(c, m, 0) == 'Z') break;
		at += 1u + rd16(c, m, 3);
	}
	return 0;
}

static void exec_release(struct dos_state *d, uint16_t owner)
{
	struct cpu *c = d->cpu;
	for (uint32_t at = d->arena_seg; at < 0xA000;) {
		uint16_t m = at;
		if (!mcb_valid(c, m)) break;
		if (rd16(c, m, 1) == owner) wr16(c, m, 1, 0);
		if (rd8(c, m, 0) == 'Z') break;
		at += 1u + rd16(c, m, 3);
	}
	for (uint32_t at = d->arena_seg; at < 0xA000;) {
		uint16_t m = at;
		if (!mcb_valid(c, m)) break;
		if (!rd16(c, m, 1)) mcb_join(c, m);
		if (rd8(c, m, 0) == 'Z') break;
		at += 1u + rd16(c, m, 3);
	}
}

static enum cpu_stop dos_exec(struct dos_state *d, struct cpu *c)
{
	int error = 1;
	if ((c->r[AX] & 255) != 0) goto fail;
	char path[256], name[256], err[256];
	unsigned n;
	for (n = 0; n < sizeof(name); n++) {
		name[n] = rd8(c, c->sreg[3], c->r[DX] + n);
		if (!name[n]) break;
	}
	if (n == sizeof(name)) { error = 3; goto fail; }
	strcpy(path, name);
	if (dos_path(path, sizeof(path)) < 0) { error = 3; goto fail; }
	uint16_t min, max;
	error = load_program_at(NULL, path, 0x1000, 0x9000, &min, &max, err, sizeof(err));
	if (error) goto fail;
	uint16_t es = c->sreg[0], bx = c->r[BX];
	uint16_t env = rd16(c, es, bx);
	if (!env) env = rd16(c, d->psp_seg, PSP_ENVIRON);
	unsigned envlen;
	for (envlen = 0; envlen < 32766; envlen++)
		if (!rd8(c, env, envlen) && !rd8(c, env, envlen + 1)) break;
	if (envlen == 32766) { error = 10; goto fail; }
	envlen += 2;
	uint16_t tail_off = rd16(c, es, bx + 2), tail_seg = rd16(c, es, bx + 4);
	uint8_t tail[128], fcbs[32];
	unsigned tail_len = rd8(c, tail_seg, tail_off);
	if (tail_len > 126) tail_len = 126;
	tail[0] = tail_len;
	for (unsigned i = 0; i < tail_len; i++) tail[i + 1] = rd8(c, tail_seg, tail_off + 1 + i);
	tail[tail_len + 1] = '\r';
	for (unsigned i = 0; i < 2; i++) {
		uint16_t off = rd16(c, es, bx + 6 + i * 4), seg = rd16(c, es, bx + 8 + i * 4);
		for (unsigned j = 0; j < 16; j++) fcbs[i * 16 + j] = (seg || off) ? rd8(c, seg, off + j) : 0;
	}
	struct dos_parent *parent = calloc(1, sizeof(*parent));
	if (!parent) { error = 8; goto fail; }
	int inherited[HANDLES];
	for (int i = 0; i < HANDLES; i++) inherited[i] = -1;
	for (int i = 0; i < HANDLES; i++) {
		if (d->handle_fd[i] < 0 || d->private_handles[i]) continue;
		inherited[i] = dup(d->handle_fd[i]);
		if (inherited[i] < 0) { error = 4; goto rollback; }
	}
	uint16_t env_paras = (envlen + 2 + strlen(name) + 1 + 15) / 16;
	uint16_t child_env = exec_alloc(d, env_paras, env_paras);
	if (!child_env) { error = 8; goto rollback; }
	for (unsigned i = 0; i < envlen; i++) wr8(c, child_env, i, rd8(c, env, i));
	wr16(c, child_env, envlen, 1);
	for (unsigned i = 0; i <= strlen(name); i++) wr8(c, child_env, envlen + 2 + i, name[i]);
	uint16_t child = exec_alloc(d, min, max);
	if (!child) { error = 8; goto rollback; }
	parent->cpu = *c;
	error = load_program_at(c, path, child, rd16(c, child - 1, 3), NULL, NULL, err, sizeof(err));
	if (error) goto rollback;
	parent->next = d->parent;
	parent->psp = d->psp_seg; parent->prog = d->prog_seg;
	parent->dta_seg = d->dta_seg; parent->dta = d->dta;
	memcpy(parent->handles, d->handle_fd, sizeof(parent->handles));
	memcpy(parent->private_handles, d->private_handles, sizeof(d->private_handles));
	memcpy(parent->handle_drive, d->handle_drive, sizeof(d->handle_drive));
	memcpy(parent->fcb, d->fcb_fd, sizeof(parent->fcb));
	memcpy(parent->find, d->find, sizeof(parent->find));
	memcpy(d->handle_fd, inherited, sizeof(inherited));
	memset(d->private_handles, 0, sizeof(d->private_handles));
	for (int i = 0; i < 16; i++) d->fcb_fd[i] = -1;
	memset(d->find, 0, sizeof(d->find));
	d->parent = parent;
	d->psp_seg = child; d->prog_seg = child + 0x10;
	d->dta_seg = child; d->dta = PSP_DTA;
	memset(c->mem + lin(child, 0), 0, 256);
	build_psp(d);
	wr16(c, child, PSP_MEMSIZE, child + rd16(c, child - 1, 3));
	wr16(c, child, PSP_PARENT, parent->psp);
	wr16(c, child, PSP_ENVIRON, child_env);
	wr16(c, child - 1, 1, child);
	wr16(c, child_env - 1, 1, child);
	for (unsigned i = 0; i < tail_len + 2; i++) wr8(c, child, PSP_TAIL + i, tail[i]);
	for (unsigned i = 0; i < sizeof(fcbs); i++) wr8(c, child, 0x5C + i, fcbs[i]);
	for (int i = 0; i < 20; i++) wr8(c, child, PSP_JFT + i, d->handle_fd[i] < 0 ? 0xFF : i);
	/* The service caller always IRETs: synthesize the child's entry frame. */
	push16(c, c->flags); push16(c, c->sreg[1]); push16(c, c->ip);
	return CPU_OK;
rollback:
	exec_release(d, 0xFFFF);
	for (int i = 0; i < HANDLES; i++) if (inherited[i] >= 0) close(inherited[i]);
	free(parent);
fail:
	c->r[AX] = error; set_cf(c, 1); return CPU_OK;
}

static enum cpu_stop exec_return(struct dos_state *d, struct cpu *c)
{
	struct dos_parent *p = d->parent;
	d->child_status = c->exit_code;
	for (int i = 0; i < HANDLES; i++) if (d->handle_fd[i] > 2) close(d->handle_fd[i]);
	for (int i = 0; i < 16; i++) if (d->fcb_fd[i] >= 0) close(d->fcb_fd[i]);
	for (int i = 0; i < 8; i++) if (d->find[i].dir) closedir(d->find[i].dir);
	for (int i = 0; i < 3; i++) {
		wr16(c, 0, (0x22 + i) * 4, rd16(c, d->psp_seg, PSP_INT22 + i * 4));
		wr16(c, 0, (0x22 + i) * 4 + 2, rd16(c, d->psp_seg, PSP_INT22 + i * 4 + 2));
	}
	exec_release(d, d->psp_seg);
	memcpy(d->handle_fd, p->handles, sizeof(p->handles));
	memcpy(d->private_handles, p->private_handles, sizeof(d->private_handles));
	memcpy(d->fcb_fd, p->fcb, sizeof(p->fcb));
	memcpy(d->find, p->find, sizeof(p->find));
	d->psp_seg = p->psp; d->prog_seg = p->prog;
	d->dta_seg = p->dta_seg; d->dta = p->dta;
	memcpy(c->r, p->cpu.r, sizeof(c->r));
	memcpy(c->r_hi, p->cpu.r_hi, sizeof(c->r_hi));
	memcpy(c->sreg, p->cpu.sreg, sizeof(c->sreg));
	c->ip = p->cpu.ip; c->flags = p->cpu.flags;
	c->flags_hi = p->cpu.flags_hi;
	c->exit_code = 0;
	d->parent = p->next;
	free(p);
	set_cf(c, 0);
	return CPU_OK;
}

static int fcb_dos_path(struct cpu *c, uint16_t seg,
		uint16_t name_off, unsigned drive, char *path, size_t size)
{
	if (drive > 26 || size < 4) return -1;
	unsigned letter = drive ? drive - 1 : (unsigned)dos_get_drive();
	if (letter > 25) return -1;
	int n = snprintf(path, size, "%c:/", 'A' + letter);
	if (n < 0 || (size_t)n >= size) return -1;
	size_t at = (size_t)n;
	for (int i = 0; i < 8; i++) {
		uint8_t ch = rd8(c, seg, name_off + i);
		if (!ch || ch == ' ') continue;
		if (ch == '?' || ch == '*') return -1;
		if (at + 1 >= size) return -1;
		path[at++] = (char)ch;
	}
	int ext = 0;
	for (int i = 0; i < 3; i++) {
		uint8_t ch = rd8(c, seg, name_off + 8 + i);
		if (ch && ch != ' ') ext++;
	}
	if (ext) {
		if (at + 1 >= size) return -1;
		path[at++] = '.';
		for (int i = 0; i < 3; i++) {
			uint8_t ch = rd8(c, seg, name_off + 8 + i);
			if (!ch || ch == ' ') continue;
			if (at + 1 >= size) return -1;
			path[at++] = (char)ch;
		}
	}
	path[at] = '\0';
	return dos_path(path, size);
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
	if (ah == 0x17) {
		char old_path[256], new_path[256];
		unsigned drive = rd8(c, seg, off);
		if (fcb_dos_path(c, seg, off + 1, drive, old_path, sizeof(old_path)) == 0 &&
		    fcb_dos_path(c, seg, off + 0x11, drive, new_path, sizeof(new_path)) == 0) {
			struct stat st;
			if (stat(new_path, &st) < 0 && errno == ENOENT && rename(old_path, new_path) == 0)
				status = 0;
		}
	} else if (ah == 0x0F || ah == 0x16) {
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
	} else if (ah == 0x10) {
		/* MS-DOS 4.0 FCBIO.ASM: successive closes of an FCB always
		 * return success, because applications close them more than
		 * once.  A never-opened FCB takes the same path there. */
		if (slot >= 0) { close(d->fcb_fd[slot]); d->fcb_fd[slot] = -1; }
		status = 0;
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

	case 0x0F: case 0x10: case 0x14: case 0x15: case 0x16: case 0x17:
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
		/* Share the BIOS clock, so IRQ0 chaining/midnight have the same
		 * meaning for DOS polling as for INT 1Ah. No fabricated wall date. */
		uint32_t ticks = mem_r16(c, 0x46C) | ((uint32_t)mem_r16(c, 0x46E) << 16);
		uint32_t centis = (uint32_t)(((uint64_t)ticks * 6553600u / 1193182u) % 8640000u);
		c->r[CX] = (uint16_t)(((centis / 360000u) << 8) | ((centis / 6000u) % 60));
		c->r[DX] = (uint16_t)((((centis / 100u) % 60) << 8) | (centis % 100));
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

	case 0x3B: { /* change directory, without switching the default drive */
		char path[256]; unsigned i;
		for (i = 0; i + 1 < sizeof(path); i++) {
			path[i] = rd8(c, ds, (uint16_t)(dx + i));
			if (!path[i]) break;
		}
		if (i + 1 == sizeof(path) || !path[0]) {
			c->r[AX] = 3; set_cf(c, 1); return CPU_OK;
		}
		int result = dos_chdir(path);
		if (result < 0) c->r[AX] = 3;
		set_cf(c, result < 0); return CPU_OK;
	}
	case 0x39: case 0x3A: { /* make/remove directory */
		char path[256]; unsigned i;
		for (i = 0; i + 1 < sizeof(path); i++) {
			path[i] = rd8(c, ds, (uint16_t)(dx + i));
			if (!path[i]) break;
		}
		if (i + 1 == sizeof(path)) { c->r[AX] = 3; set_cf(c, 1); return CPU_OK; }
		if (dos_directory(path, ah == 0x3A) < 0) {
			c->r[AX] = errno == ENOENT || errno == ENOTDIR ? 3 :
			             errno == EBUSY ? 16 : 5;
			set_cf(c, 1);
		} else set_cf(c, 0);
		return CPU_OK;
	}

	case 0x3C: case 0x3D: case 0x3E: case 0x3F: case 0x40: case 0x41:
	case 0x42: case 0x43: case 0x4E: case 0x4F: case 0x56: case 0x57:
	case 0x5B:
		return dos_file(d, c, ah);

	case 0x44: { /* IOCTL get device information (4400h). */
		if ((c->r[AX] & 255) != 0) {
			c->r[AX] = 1; set_cf(c, 1); return CPU_OK;
		}
		if (bx >= HANDLES || d->handle_fd[bx] < 0) {
			c->r[AX] = 6; set_cf(c, 1); return CPU_OK;
		}
		/* Classify the actual descriptor: stdin can be a file, and a
		 * duplicate console descriptor can have any DOS handle number.
		 * CON supports both input and output. Other IOCTL functions
		 * remain unsupported rather than silently claiming success. */
		/* CON: character-driver attribute, not at EOF, device,
		 * INT29 recipient, console input and console output. */
		c->r[DX] = isatty(d->handle_fd[bx]) ? 0x80D3 : d->handle_drive[bx];
		set_cf(c, 0);
		return CPU_OK;
	}

	case 0x45: case 0x46: { /* duplicate / force duplicate file handle */
		if (bx >= HANDLES || d->handle_fd[bx] < 0) {
			c->r[AX] = 6; set_cf(c, 1); return CPU_OK;
		}
		int target = ah == 0x46 ? c->r[CX] : 0;
		if (ah == 0x45) while (target < HANDLES && d->handle_fd[target] >= 0) target++;
		if (target >= HANDLES) { c->r[AX] = ah == 0x46 ? 6 : 4; set_cf(c, 1); return CPU_OK; }
		if (target != bx) {
			int fd = dup(d->handle_fd[bx]);
			if (fd < 0) { c->r[AX] = 4; set_cf(c, 1); return CPU_OK; }
			if (d->handle_fd[target] > 2) close(d->handle_fd[target]);
			d->handle_fd[target] = fd;
			d->private_handles[target] = d->private_handles[bx];
			d->handle_drive[target] = d->handle_drive[bx];
		}
		c->r[AX] = target; set_cf(c, 0); return CPU_OK;
	}
	case 0x48: case 0x49: case 0x4A:
		return dos_memory(d, c, ah);

	case 0x47: { /* current directory, without drive or leading slash */
		int drive = (dx & 255) ? (dx & 255) - 1 : dos_get_drive();
		char cwd[64];
		if (dos_getcwd(drive, cwd, sizeof(cwd)) < 0) {
			c->r[AX] = 15; set_cf(c, 1); return CPU_OK;
		}
		for (unsigned i = 0; ; i++) {
			wr8(c, ds, (uint16_t)(c->r[SI] + i), cwd[i]);
			if (!cwd[i]) break;
		}
		set_cf(c, 0); return CPU_OK;
	}
	case 0x71: /* no Windows long-filename API; callers can use DOS 8.3 */
		c->r[AX] = 0x7100; set_cf(c, 1); return CPU_OK;

	case 0x4B:
		return dos_exec(d, c);

	case 0x4C: /* terminate with a return code */
		d->exit_code = (uint8_t)(c->r[AX] & 0xFF);
		c->exit_code = d->exit_code;
		return CPU_EXIT;

	case 0x4D: /* last child status, consumed by this call */
		c->r[AX] = d->child_status;
		d->child_status = 0;
		return CPU_OK;

	case 0x51: case 0x62: /* get the current PSP */
		c->r[BX] = d->psp_seg;
		return CPU_OK;

	case 0x52: /* DOS list of lists; first MCB pointer is at ES:BX-2 */
		c->sreg[0] = 0x0E00;
		c->r[BX] = 2;
		return CPU_OK;

	default:
		/* A function we don't have: refuse it the way DOS does. */
		set_cf(c, 1);
		c->r[AX] = 0x0001; /* invalid function */
		return CPU_OK;
	}
}

/* --- the file services --- */

static int handle_alloc(struct dos_state *d, int fd, uint16_t seg, uint16_t off)
{
	for (int i = 0; i < HANDLES; i++) {
		if (d->handle_fd[i] < 0) {
			d->handle_fd[i] = fd;
			d->private_handles[i] = 0;
			unsigned ch = rd8(d->cpu, seg, off);
			d->handle_drive[i] = isalpha(ch) && rd8(d->cpu, seg, (uint16_t)(off + 1)) == ':'
			    ? (uint8_t)(toupper(ch) - 'A') : (uint8_t)dos_get_drive();
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

/* Some DOS file managers collect a replacement name through BIOS keyboard
 * calls but pass the current directory (or an empty pathname) to their DOS
 * file operation. When that happens, complete the target with the last
 * submitted console name. A normal, non-directory pathname always wins. */
static int path_complete_from_console(struct dos_state *d, char *path, size_t n)
{
	struct stat st;
	if (!d->last_console_line_ready || !d->last_console_line[0] ||
	    stat(path, &st) < 0 || !S_ISDIR(st.st_mode)) return 0;
	char name[sizeof(d->last_console_line)];
	snprintf(name, sizeof(name), "%s", d->last_console_line);
	if (dos_path(name, sizeof(name)) < 0 || strlen(name) >= n) return -1;
	snprintf(path, n, "%s", name);
	d->last_console_line_ready = 0;
	return 1;
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
		/* DOS search records carry the directory entry's actual attributes.
		 * ATTRIB and similar tools rely on this field when listing files. */
		if (fileattr(path, &attr, 0) < 0)
			attr = S_ISDIR(st.st_mode) ? 0x10 : 0x20;
		if ((attr & 0x10) && !(d->find[slot].attr & 0x10)) continue;
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

/* The file services (INT 21h AH=3C..43, 4E, 4F, 57). */
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
		if (path_complete_from_console(d, path, sizeof(path)) < 0) break;
		/* O_CREAT: DOS creates the file when it isn't there. The mode
		 * is 0644 rather than open's usual 0666 & ~umask, so a
		 * program's files are not left world-writable. */
		fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, 0644);
		if (fd < 0) {
			break;
		}
		int h = handle_alloc(d, fd, ds, dx);
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
		int access = c->r[AX] & 7;
		if (access > 2) { c->r[AX] = 12; set_cf(c, 1); return CPU_OK; }
		int mode = access == 0 ? O_RDONLY : access == 1 ? O_WRONLY : O_RDWR;
		fd = open(path, mode);
		if (fd < 0) {
			break;
		}
		int h = handle_alloc(d, fd, ds, dx);
		if (h < 0) {
			close(fd);
			break;
		}
		d->private_handles[h] = !!(c->r[AX] & 0x80);
		c->r[AX] = (uint16_t)h;
		set_cf(c, 0);
		return CPU_OK;
	}
	case 0x3E: { /* close, including redirected standard handles */
		if (bx >= HANDLES || d->handle_fd[bx] < 0) {
			c->r[AX] = 6; set_cf(c, 1); return CPU_OK;
		}
		/* The initial console fds belong to the runner; DOS owns only
		 * its handle entry. Opened/duplicated files have separate fds. */
		if (d->handle_fd[bx] > 2 && close(d->handle_fd[bx]) < 0) {
			c->r[AX] = 6; set_cf(c, 1); return CPU_OK;
		}
		d->handle_fd[bx] = -1;
		d->private_handles[bx] = 0;
		set_cf(c, 0);
		return CPU_OK;
	}
	case 0x3F: { /* read */
		
		if (bx >= HANDLES || d->handle_fd[bx] < 0) {
			c->r[AX] = 6; set_cf(c, 1); return CPU_OK;
		}
		uint16_t n = cx;
		uint8_t *buf = malloc(n ? n : 1);
		if (!buf) {
			break;
		}
		long got;
		if (d->handle_fd[bx] == d->con_in) {
			got = 0;
			while ((uint16_t)got < n) {
				if (got && con_peek(d) < 0) break;
				int ch = con_getc(d);
				if (ch < 0) break;
				buf[got++] = (uint8_t)ch;
			}
		} else {
			got = read(d->handle_fd[bx], buf, n);
		}
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
			c->r[AX] = 6; set_cf(c, 1); return CPU_OK;
		}
		uint16_t n = cx;
		uint8_t *buf = malloc(n ? n : 1);
		if (!buf) {
			break;
		}
		for (uint16_t i = 0; i < n; i++) {
			buf[i] = rd8(c, ds, (uint16_t)(dx + i));
		}
		/* The console is a teletype into the emulated text screen, the
		 * way DOS's CON driver writes B800 memory: a program that mixes
		 * string writes with direct cells and reads the screen back sees
		 * one coherent picture. Redirected handles and the host harness
		 * keep the raw stream. */
		#ifdef MANIOS_VERSION
		if (d->handle_fd[bx] == d->con_out || d->handle_fd[bx] == 2) {
			for (uint16_t i = 0; i < n; i++) con_putc(d, buf[i]);
			free(buf);
			c->r[AX] = n;
			set_cf(c, 0);
			return CPU_OK;
		}
		#endif
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
	case 0x56: { /* rename: DS:DX old name, ES:DI new name */
		char old_path[256], new_path[256];
		if (path_at(d, ds, dx, old_path, sizeof(old_path)) < 0 ||
		    path_at(d, c->sreg[0], c->r[DI], new_path, sizeof(new_path)) < 0) {
			c->r[AX] = 3; set_cf(c, 1); return CPU_OK;
		}
		if (path_complete_from_console(d, new_path, sizeof(new_path)) < 0) {
			c->r[AX] = 3; set_cf(c, 1); return CPU_OK;
		}
		if (rename(old_path, new_path) < 0) {
			c->r[AX] = errno == ENOENT ? 2 : errno == ENOTDIR ? 3 :
			             errno == EXDEV ? 17 : errno == EEXIST ? 80 : 5;
			set_cf(c, 1); return CPU_OK;
		}
		set_cf(c, 0);
		return CPU_OK;
	}
	case 0x5B: { /* create new: DS:DX name, CX attributes */
		if (path_at(d, ds, dx, path, sizeof(path)) < 0) {
			c->r[AX] = 3; set_cf(c, 1); return CPU_OK;
		}
		if (path_complete_from_console(d, path, sizeof(path)) < 0) {
			c->r[AX] = 3; set_cf(c, 1); return CPU_OK;
		}
		fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
		if (fd < 0) {
			c->r[AX] = errno == EEXIST ? 80 : errno == ENOENT || errno == ENOTDIR ? 3 : 5;
			set_cf(c, 1); return CPU_OK;
		}
		int h = handle_alloc(d, fd, ds, dx);
		if (h < 0) { close(fd); unlink(path); c->r[AX] = 4; set_cf(c, 1); return CPU_OK; }
		c->r[AX] = (uint16_t)h;
		set_cf(c, 0);
		return CPU_OK;
	}
	case 0x42: { /* lseek: CX:DX is the offset, AL the whence */
		
		if (bx >= HANDLES || d->handle_fd[bx] < 0) {
			c->r[AX] = 6; set_cf(c, 1); return CPU_OK;
		}
		int whence = c->r[AX] & 0xFF;
		if (whence > 2) {
			c->r[AX] = 1; set_cf(c, 1); return CPU_OK;
		}
		/* Sign extend the DOS displacement before widening to host off_t. */
		int32_t off = (int32_t)((uint32_t)cx << 16 | dx);
		off_t pos = lseek(d->handle_fd[bx], off,
		                 whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END);
		if (pos < 0 || (uint64_t)pos > UINT32_MAX) {
			c->r[AX] = pos < 0 && errno == EBADF ? 6 : 25;
			set_cf(c, 1); return CPU_OK;
		}
		c->r[AX] = (uint16_t)(pos & 0xFFFF);
		c->r[DX] = (uint16_t)((pos >> 16) & 0xFFFF);
		set_cf(c, 0);
		return CPU_OK;
	}
	case 0x57: { /* get/set the handle's last modification time */
		unsigned sub = c->r[AX] & 0xFF;
		if (sub > 1) { c->r[AX] = 1; set_cf(c, 1); return CPU_OK; }
		if (bx >= HANDLES || d->handle_fd[bx] < 0) {
			c->r[AX] = 6; set_cf(c, 1); return CPU_OK;
		}
		uint32_t stamp = (uint32_t)dx << 16 | cx;
		if (filetime(d->handle_fd[bx], &stamp, sub) < 0) {
			c->r[AX] = errno == EBADF ? 6 : errno == ENOSYS ? 1 : 5;
			set_cf(c, 1); return CPU_OK;
		}
		if (!sub) { c->r[CX] = (uint16_t)stamp; c->r[DX] = (uint16_t)(stamp >> 16); }
		set_cf(c, 0); return CPU_OK;
	}
	case 0x43: { /* get/set the file attributes */
		unsigned sub = c->r[AX] & 0xFF;
		uint8_t attr = (uint8_t)c->r[CX];
		if (sub > 1 || (sub == 1 && (attr & (uint8_t)~0x27u))) {
			c->r[AX] = sub > 1 ? 1 : 12; set_cf(c, 1); return CPU_OK;
		}
		if (path_at(d, ds, dx, path, sizeof(path)) < 0) {
			c->r[AX] = 3; set_cf(c, 1); return CPU_OK;
		}
		if (fileattr(path, &attr, sub == 1) < 0) {
			c->r[AX] = errno == ENOENT ? 2 : errno == ENOTDIR ? 3 : errno == EROFS || errno == EACCES ? 5 : 1;
			set_cf(c, 1); return CPU_OK;
		}
		if (!sub) c->r[CX] = attr;
		set_cf(c, 0); return CPU_OK;
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

static void text_position(struct dos_state *d, uint16_t pos);
static void text_colour(struct dos_state *d, uint8_t attr);

/* Mirror direct writes to the active 80x25 colour text page. DOS/BIOS
 * output updates text_seen itself, so ordinary terminal output is not
 * redrawn at absolute screen coordinates. Only changed spans are sent. */
static void text_present(struct dos_state *d)
{
	/* BIOS modes 2 and 3 are both 80x25 text modes backed by the
	 * colour text page. Legacy programs may select mode 2 even on VGA. */
	if (d->video_mode != 2 && d->video_mode != 3) return;
	uint32_t base = 0xB8000 + d->active_page * 4096;
	int changed = 0;
	for (unsigned at = 0; at < 80 * 25;) {
		uint16_t cell = mem_r16(d->cpu, base + at * 2);
		if (cell == d->text_seen[at]) { at++; continue; }
		unsigned start = at, end = (at / 80 + 1) * 80;
		uint8_t attr = cell >> 8;
		char run[80]; unsigned n = 0;
		while (at < end) {
			cell = mem_r16(d->cpu, base + at * 2);
			if (cell == d->text_seen[at] || (cell >> 8) != attr) break;
			run[n++] = (cell & 255) ? (char)cell : ' ';
			d->text_seen[at++] = cell;
		}
		text_position(d, ((start / 80) << 8) | (start % 80));
		text_colour(d, attr); write(d->con_out, run, n); changed = 1;
	}
	if (changed) {
		write(d->con_out, "\033[0m", 4);
		text_position(d, d->cursor[d->active_page]);
	}
}

/* Copies the program's mode 13h framebuffer to ManiOS's screen. The
 * program's pixels are one byte each (RGB 3-3-2); ManiOS's canvas is
 * 32-bit, so each byte is expanded to a colour. */
void dos_video_present(struct dos_state *d)
{
	if (!d->screen_open) {
		video_screen_save(d);
		text_present(d);
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

/* Translate the BIOS cursor scanline shape to the closest ANSI terminal
 * cursor. CH bit 5 disables the cursor; the remaining shape is approximate
 * because terminals expose block, underline and bar styles rather than VGA
 * scanlines. */
static void text_cursor_shape(struct dos_state *d)
{
	unsigned start = (d->cursor_shape >> 8) & 0x1F;
	unsigned end = d->cursor_shape & 0x1F;
	if (d->cursor_shape & 0x2000) {
		write(d->con_out, "\033[?25l", 6);
		return;
	}
	write(d->con_out, "\033[?25h", 6);
	unsigned style = 2; /* steady block */
	if (start >= 12 && end >= 12) style = 4; /* steady underline */
	else if (start >= 7 && end >= 7) style = 6; /* steady bar */
	char seq[16]; int n = snprintf(seq, sizeof(seq), "\033[%u q", style);
	write(d->con_out, seq, n);
}

static void text_mode_reset(struct dos_state *d)
{
	/* BIOS mode set clears all text pages and returns to page zero with the
	 * conventional 80x25 cursor position. */
	for (int page = 0; page < 8; page++) {
		uint32_t base = 0xB8000 + page * 4096;
		for (unsigned cell = 0; cell < 80 * 25; cell++)
			mem_w16(d->cpu, base + cell * 2, 0x0720);
		d->cursor[page] = 0;
	}
	d->active_page = 0;
	for (unsigned cell = 0; cell < 80 * 25; cell++) d->text_seen[cell] = 0x0720;
	write(d->con_out, "\033[0m\033[2J\033[H", 11);
	text_cursor_shape(d);
	text_position(d, 0);
}

/* BIOS INT 10h AH=06h/07h scrolls a rectangular text window. The active
 * page is mirrored to the terminal by text_present(), which compares the
 * updated cells with its cache and repaints the changed spans. */
static void text_scroll(struct dos_state *d, int page, int top, int left,
			int bottom, int right, int lines, int down, uint8_t attr)
{
	if (top < 0 || left < 0 || bottom >= 25 || right >= 80 ||
	    top > bottom || left > right) return;
	int height = bottom - top + 1;
	int clear_all = lines == 0 || lines >= height;
	if (lines < 0) lines = 0;
	if (lines > height) lines = height;
	uint32_t base = 0xB8000 + page * 4096;
	uint16_t blank = (uint16_t)(' ' | (attr << 8));
	if (lines == 0) {
		for (int row = top; row <= bottom; row++)
			for (int col = left; col <= right; col++)
				mem_w16(d->cpu, base + (row * 80 + col) * 2, blank);
	} else if (!down) {
		for (int row = top; row <= bottom - lines; row++)
			for (int col = left; col <= right; col++)
				mem_w16(d->cpu, base + (row * 80 + col) * 2,
					mem_r16(d->cpu, base + ((row + lines) * 80 + col) * 2));
		for (int row = bottom - lines + 1; row <= bottom; row++)
			for (int col = left; col <= right; col++)
				mem_w16(d->cpu, base + (row * 80 + col) * 2, blank);
	} else {
		for (int row = bottom; row >= top + lines; row--)
			for (int col = left; col <= right; col++)
				mem_w16(d->cpu, base + (row * 80 + col) * 2,
					mem_r16(d->cpu, base + ((row - lines) * 80 + col) * 2));
		for (int row = top; row < top + lines; row++)
			for (int col = left; col <= right; col++)
				mem_w16(d->cpu, base + (row * 80 + col) * 2, blank);
	}
	if (page == d->active_page) {
		/* INT 10h's full-window clear must clear the visible terminal even
		 * when the DOS render cache already contains blank cells. The
		 * terminal also keeps the cursor in place for CSI 2 J. */
		if (clear_all && top == 0 && left == 0 && bottom == 24 && right == 79) {
			write(d->con_out, "\033[2J", 4);
			text_position(d, d->cursor[page]);
		}
		text_present(d);
	}
}

static enum cpu_stop bios10(struct dos_state *d, struct cpu *c)
{
	uint8_t ah = (uint8_t)(c->r[AX] >> 8);
	uint8_t al = (uint8_t)(c->r[AX] & 0xFF);
	switch (ah) {
	case 0x00: /* set the video mode */
		d->video_mode = al;
		wr8(c, 0x40, 0x49, al);
		if (al == 0x13) {
			/* 320x200, 256 colours: ManiOS's VGA mode 13h. */
			dos_video_mode13(d);
		} else if (al == 0x02 || al == 0x03) {
			dos_video_text(d);
			d->cursor_shape = 0x0607;
			wr8(c, 0x40, 0x84, 24);
			wr16(c, 0x40, 0x4A, 80);
			wr16(c, 0x40, 0x4C, 0x1000);
			wr16(c, 0x40, 0x4E, 0);
			wr8(c, 0x40, 0x62, 0);
			for (int page = 0; page < 8; page++) wr16(c, 0x40, 0x50 + page * 2, 0);
			wr16(c, 0x40, 0x60, d->cursor_shape);
			text_mode_reset(d);
		}
		return CPU_OK;
	case 0x01: d->cursor_shape = c->r[CX]; text_cursor_shape(d); return CPU_OK;
	case 0x02: {
		int page = (c->r[BX] >> 8) & 7;
		uint16_t pos = c->r[DX];
		if ((pos >> 8) < 25 && (pos & 255) < 80) {
			d->cursor[page] = pos;
			wr16(c, 0x40, 0x50 + page * 2, pos);
			if (page == d->active_page) text_position(d, pos);
		}
		return CPU_OK;
	}
	case 0x03:
		c->r[DX] = d->cursor[(c->r[BX] >> 8) & 7]; c->r[CX] = d->cursor_shape; return CPU_OK;
	case 0x05:
		d->active_page = al & 7;
		wr8(c, 0x40, 0x62, d->active_page);
		wr16(c, 0x40, 0x4E, d->active_page * 0x1000);
		/* text_seen is the render cache for the visible page. Comparing it
		 * against the newly active page repaints only cells that differ. */
		text_present(d);
		text_cursor_shape(d);
		return CPU_OK;
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
			if (page == d->active_page) d->text_seen[start + i] = mem_r16(c, at);
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
	case 0x06: case 0x07: {
		int top = c->r[CX] >> 8, left = c->r[CX] & 255;
		int bottom = c->r[DX] >> 8, right = c->r[DX] & 255;
		text_scroll(d, d->active_page, top, left, bottom, right, al,
			    ah == 0x07, (uint8_t)(c->r[BX] >> 8));
		return CPU_OK;
	}
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
		int ch = bios_key_get(d);
		if (ch < 0) {
			ch = 0x1A;
		}
		c->r[AX] = key_word(ch);
		return CPU_OK;
	}
	case 0x01: case 0x11: /* key ready? ZF set if not */
		if (bios_key_peek(d) < 0) { c->flags |= FLAG_ZF; c->r[AX] = 0; }
		else { c->flags &= ~FLAG_ZF; c->r[AX] = key_word(d->pending_bios_key >= 0 ? d->pending_bios_key : d->pending_key); }
		return CPU_OK;
	case 0x02: /* shift status */
		c->r[AX] = (uint16_t)(c->r[AX] & 0xFF00);
		return CPU_OK;
	default:
		return CPU_OK;
	}
}

/* Convert wall milliseconds to 1,193,182-Hz oscillator clocks without
 * dropping fractional clocks. PIT0 controls the IRQ rate; only the BIOS
 * handler advances 0040:006C, including when a guest chains to it. */
void dos_clock_advance(struct dos_state *d, uint32_t elapsed_ms)
{
	uint64_t units = (uint64_t)elapsed_ms * 1193182u + d->clock_fraction;
	d->clock_fraction = (uint32_t)(units % 1000u);
	dos_timer_advance(d, units / 1000u);
}

/* INT 1Ah: the clock. */
static enum cpu_stop bios1a(struct dos_state *d, struct cpu *c)
{
	(void)d;
	uint8_t ah = (uint8_t)(c->r[AX] >> 8);
	switch (ah) {
	case 0x00: /* read the tick count: CX:DX, AL rolled over */
		c->r[CX] = mem_r16(c, 0x46E);
		c->r[DX] = mem_r16(c, 0x46C);
		c->r[AX] = (uint16_t)((c->r[AX] & 0xFF00) | mem_r8(c, 0x470));
		mem_w8(c, 0x470, 0);
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

/* Return the BIOS vector represented by a live INT F3 ROM-stub frame.
 * INT F3 is private to these exact guest stubs; ordinary guest use is
 * rejected so the internal bridge cannot become another public API. */
static int bios_stub_target(struct cpu *c)
{
	static const uint8_t vectors[] = {0x10,0x11,0x12,0x14,0x15,0x16,0x17,0x1A,0x29};
	if (rd16(c,c->sreg[2],c->r[SP]+2) != 0xF000) return -1;
	uint16_t ret = rd16(c,c->sreg[2],c->r[SP]);
	for (unsigned i = 0; i < sizeof(vectors); i++)
		if (ret == 0x142 + i * 3 &&
		    rd8(c,0xF000,(uint16_t)(ret-2)) == 0xCD &&
		    rd8(c,0xF000,(uint16_t)(ret-1)) == 0xF3 &&
		    rd8(c,0xF000,ret) == 0xCF)
			return vectors[i];
	return -1;
}

static enum cpu_stop dispatch_int(struct dos_state *d, struct cpu *c, uint8_t vector)
{
	switch (vector) {
	case 0xF1: { /* Internal BIOS INT 08h ROM trap, never a guest API. */
		if (rd16(c,c->sreg[2],c->r[SP]+2) != 0xF000 || rd16(c,c->sreg[2],c->r[SP]) != 0x102)
			return CPU_IRET;
		uint32_t ticks = mem_r16(c,0x46C) | (uint32_t)mem_r16(c,0x46E)<<16;
		if (++ticks >= 0x1800B0u) { ticks = 0; mem_w8(c,0x470,1); }
		mem_w16(c,0x46C,(uint16_t)ticks); mem_w16(c,0x46E,(uint16_t)(ticks>>16));
		return CPU_OK;
	}
	case 0xF2: /* Internal INT 21h ROM-stub trap; reject direct guest use. */
		if (rd16(c,c->sreg[2],c->r[SP]) != 0x132 ||
		    rd16(c,c->sreg[2],c->r[SP] + 2) != 0xF000)
			return CPU_IRET;
		return dos21(d, c);
	case 0xF3: { /* Internal BIOS ROM-stub trap for far-call IVT dispatch. */
		int target = bios_stub_target(c);
		if (target == 0x16 &&
		    ((c->r[AX] >> 8) == 0 || (c->r[AX] >> 8) == 0x10))
			dos_video_present(d);
		return target < 0 ? CPU_IRET : dispatch_int(d, c, (uint8_t)target);
	}
	case 0x20: /* terminate */
		c->exit_code = 0;
		return CPU_EXIT;
	case 0x21:
		return dos21(d, c);
	case 0x29: /* DOS fast console output: AL, all registers preserved. */
		con_putc(d, (uint8_t)c->r[AX]);
		return CPU_OK;
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
	case 0x12: /* The BIOS memory-size word, in contiguous KiB. */
		c->r[AX] = mem_r16(c, 0x413);
		return CPU_OK;
	case 0x15:
		/* No AT system services or linear extended-memory map is
		 * implemented. XMS/EMS pools are accessed through their APIs;
		 * they must not be advertised as directly addressable RAM. */
		c->r[AX] = (uint16_t)((c->r[AX] & 255) | 0x8600);
		set_cf(c, 1);
		return CPU_OK;
	case 0x11: case 0x14: case 0x17:
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
	/* A nonzero IVT entry was resolved by cpu_enter_interrupt. Execute
	 * guest hooks (including BIOS ROM stubs), rather than discarding them. */
	uint16_t target_off = rd16(c,0,vector*4), target_seg = rd16(c,0,vector*4+2);
	if ((target_seg || target_off) && c->sreg[1] == target_seg && c->ip == target_off) {
		if (vector == 0x16 && ((c->r[AX] >> 8) == 0 || (c->r[AX] >> 8) == 0x10))
			dos_video_present(d);
		return CPU_GUEST;
	}
	if (vector == 0x16 && ((c->r[AX] >> 8) == 0 || (c->r[AX] >> 8) == 0x10)) dos_video_present(d);
	enum cpu_stop stop = dispatch_int(d, c, vector);
	if (stop == CPU_EXIT && d->parent) stop = exec_return(d, c);
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
		/* The INT 21h ROM stub returns with IRET to the guest's original
		 * interrupt/far-call frame. Propagate DOS status flags there too. */
		if ((vector == 0xF2 &&
		    rd16(c,c->sreg[2],c->r[SP]) == 0x132 &&
		    rd16(c,c->sreg[2],c->r[SP] + 2) == 0xF000) ||
		    (vector == 0xF3 && bios_stub_target(c) >= 0)) {
			uint16_t outer = (uint16_t)(c->r[SP] + 10);
			saved = rd16(c, c->sreg[2], outer);
			wr16(c, c->sreg[2], outer,
			     (uint16_t)((saved & ~status) | (c->flags & status)));
		}
	}
	return stop;
}
