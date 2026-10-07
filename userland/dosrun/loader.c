/* The .COM and .EXE loader (loader.h). A .COM is the file's bytes at
 * offset 0x100 of the program's segment, with the stack at the top of
 * the segment. An .EXE has the MZ header (EXE.INC, MIT): the image
 * follows the header, and the relocation table lists the words to add
 * the load segment to.
 */
#include "loader.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Where the program goes: the PSP at 0x1000, the program's segment just
 * after it, and the stack at the top of the program's segment. */
#define PSP_SEG  0x1000
#define PROG_SEG 0x1010
#define STACK_TOP 0xFFFE

static int read_all(const char *path, uint8_t **out, long *len)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		return -1;
	}
	long n = 0, cap = 65536;
	uint8_t *buf = malloc((size_t)cap);
	if (!buf) {
		close(fd);
		return -1;
	}
	for (;;) {
		if (n == cap) {
			cap *= 2;
			uint8_t *big = realloc(buf, (size_t)cap);
			if (!big) {
				free(buf);
				close(fd);
				return -1;
			}
			buf = big;
		}
		long got = read(fd, buf + n, (size_t)(cap - n));
		if (got <= 0) {
			break;
		}
		n += got;
	}
	close(fd);
	*out = buf;
	*len = n;
	return 0;
}

static uint16_t le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

/* The PSP and the program's segment, shared with dosapi.c through
 * dos_set_program(). */
int load_program(struct cpu *c, const char *path, const char *tail,
                 char *err, unsigned long errlen)
{
	(void)tail; /* the PSP's tail is dos_set_tail()'s job */
	uint8_t *file = NULL;
	long len = 0;
	if (read_all(path, &file, &len) < 0) {
		snprintf(err, errlen, "can't read %s", path);
		return -1;
	}

	int is_exe = len >= 2 && file[0] == 'M' && file[1] == 'Z';
	uint16_t load_seg = PROG_SEG;
	uint16_t start_cs, start_ip, start_ss, start_sp;

	if (is_exe) {
		if (len < 28) {
			snprintf(err, errlen, "%s: the EXE header is too short", path);
			free(file);
			return -1;
		}
		uint16_t len_mod = le16(file + 2);
		uint16_t pages = le16(file + 4);
		uint16_t reloc_n = le16(file + 6);
		uint16_t header_paras = le16(file + 8);
		uint16_t ss = le16(file + 14), sp = le16(file + 16);
		uint16_t ip = le16(file + 20), cs = le16(file + 22);
		uint16_t reloc_off = le16(file + 24);

		long file_len = pages ? (long)(pages - 1) * 512 + (len_mod ? len_mod : 512) : 0;
		long header_len = (long)header_paras * 16;
		if (file_len > len) {
			file_len = len;
		}
		long image_len = file_len - header_len;
		if (image_len < 0) {
			image_len = 0;
		}
		if (image_len > 0x10000 - 0x100) {
			image_len = 0x10000 - 0x100;
		}
		/* The image goes at offset 0 of the program's segment. */
		for (long i = 0; i < image_len; i++) {
			wr8(c, load_seg, (uint16_t)i, file[header_len + i]);
		}
		/* The relocations: each is an offset in the image; the word
		 * there gets the load segment added. */
		for (int i = 0; i < reloc_n; i++) {
			long e = (long)reloc_off + i * 4;
			if (e + 4 > len) {
				break;
			}
			uint16_t off = le16(file + e);
			uint16_t seg = le16(file + e + 2);
			uint32_t at = lin((uint16_t)(load_seg + seg), off);
			mem_w16(c, at, (uint16_t)(mem_r16(c, at) + load_seg));
		}
		start_cs = (uint16_t)(load_seg + cs);
		start_ip = ip;
		start_ss = (uint16_t)(load_seg + ss);
		start_sp = sp;
	} else {
		/* A .COM: the whole file at offset 0x100 of the PSP's own segment,
	 * with CS=DS=ES=SS=that segment, as DOS starts a .COM. */
		if (len > 0xFF00) {
			snprintf(err, errlen, "%s: a COM file can't be that big", path);
			free(file);
			return -1;
		}
		load_seg = PSP_SEG;
		for (long i = 0; i < len; i++) {
			wr8(c, load_seg, (uint16_t)(0x100 + i), file[i]);
		}
		start_cs = load_seg;
		start_ip = 0x100;
		start_ss = load_seg;
		start_sp = STACK_TOP;
	}
	free(file);

	/* The PSP, at the segment just before the program. DS and ES point
	 * at the PSP, as DOS starts a program; for a .COM that is also the
	 * program's own segment (load_seg was set to it above). */
	c->sreg[1] = start_cs;
	c->ip = start_ip;
	c->sreg[2] = start_ss;
	c->r[SP] = start_sp;
	c->sreg[3] = PSP_SEG;
	c->sreg[0] = PSP_SEG;
	c->r[AX] = 0;
	c->r[BX] = 0;
	c->r[CX] = 0;
	c->r[DX] = 0;
	c->r[SI] = 0;
	c->r[DI] = 0;
	c->r[BP] = 0;
	c->flags = 0x0202; /* IF set, bit 1 set */
	return 0;
}
