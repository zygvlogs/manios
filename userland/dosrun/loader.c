/* The .COM and .EXE loader (loader.h). A .COM is the file's bytes at
 * offset 0x100 of the program's segment, with the stack at the top of
 * the segment. An .EXE has the MZ header (EXE.INC, MIT): the image
 * follows the header, and the relocation table lists the words to add
 * the load segment to.
 */
#include "loader.h"

#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Where the program goes: the PSP at 0x1000, the program's segment just
 * after it, and the stack at the top of the program's segment. */
#define PSP_SEG  0x1000
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
		if (got < 0) {
			free(buf);
			close(fd);
			return -1;
		}
		if (got == 0) {
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

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)le16(p) | ((uint32_t)le16(p + 2) << 16);
}

/* The PSP and the program's segment, shared with dosapi.c through
 * dos_set_program(). */
int load_program_at(struct cpu *c, const char *path, uint16_t psp, uint16_t paras,
                    uint16_t *minimum, uint16_t *maximum, char *err, unsigned long errlen)
{
	uint8_t *file = NULL;
	long len = 0;
	if (read_all(path, &file, &len) < 0) {
		int error = errno;
		snprintf(err, errlen, "can't read %s", path);
		return error == ENOENT ? 2 : error == ENOMEM ? 8 : 5;
	}
	/* Warcraft's installation media uses LHA files named .EXA. They
	 * contain executables, but are not COM programs themselves. */
	if (len >= 7 && !memcmp(file + 2, "-lh", 3) && file[6] == '-') {
		snprintf(err, errlen, "%s: LHA archive; extract it before running", path);
		free(file);
		return 11;
	}

	int is_exe = len >= 2 && file[0] == 'M' && file[1] == 'Z';
	uint16_t load_seg = psp + 0x10;
	uint16_t start_cs, start_ip, start_ss, start_sp;

	if (is_exe) {
		if (len < 28) {
			snprintf(err, errlen, "%s: the EXE header is too short", path);
			free(file);
			return 11;
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
		/* Older linkers may encode a full last page as 512, not zero. */
		if (!pages || len_mod > 512 || header_len < 28 ||
		    header_len > file_len || file_len > len ||
		    (reloc_n && (reloc_off < 28 ||
		                  (long)reloc_off + (long)reloc_n * 4 > header_len))) {
			snprintf(err, errlen, "%s: invalid or truncated MZ executable", path);
			free(file);
			return 11;
		}
		/* An MZ stub is also the front of a DOS extender executable.
		 * Running only that stub cannot run its protected-mode payload. */
		if (header_len >= 64 && len >= 64) {
			uint32_t next = le32(file + 60);
			if (next >= (uint32_t)header_len && next <= (uint32_t)len - 2 &&
			    (!memcmp(file + next, "LE", 2) || !memcmp(file + next, "LX", 2))) {
				snprintf(err, errlen,
				         "%s: requires 32-bit protected mode (LE/LX DOS extender); dosrun supports real mode only",
				         path);
				free(file);
				return 11;
			}
		}
		long image_len = file_len - header_len;
		uint32_t base = lin(load_seg, 0);
		if ((uint32_t)image_len > 0xA0000u - base) {
			snprintf(err, errlen, "%s: EXE image exceeds conventional memory", path);
			free(file);
			return 11;
		}
		uint32_t image_paras = ((uint32_t)image_len + 15) / 16 + 0x10;
		uint32_t min = image_paras + le16(file + 10);
		uint32_t max = image_paras + le16(file + 12);
		/* Zero min/max requests the largest block, loaded at its high end. */
		if (!le16(file + 10) && !le16(file + 12)) {
			max = 0xFFFF;
			if (image_paras <= paras) {
				load_seg = psp + paras - (image_paras - 0x10);
				base = lin(load_seg, 0);
			}
		}
		if (max < min) max = min;
		if (minimum) *minimum = min > 0xFFFF ? 0xFFFF : min;
		if (maximum) *maximum = max > 0xFFFF ? 0xFFFF : max;
		if (min > paras) {
			snprintf(err, errlen, "%s: insufficient DOS memory", path);
			free(file); return 8;
		}
		/* The relocations: each is an offset in the image; the word
		 * there gets the load segment added. */
		for (int i = 0; i < reloc_n; i++) {
			long e = (long)reloc_off + i * 4;
			uint16_t off = le16(file + e);
			uint16_t seg = le16(file + e + 2);
			uint32_t relative = (uint32_t)seg * 16 + off;
			if (relative + 1 >= (uint32_t)image_len) {
				snprintf(err, errlen, "%s: EXE relocation is outside its image", path);
				free(file);
				return 11;
			}

		}
		if (c) {
			/* Linear addresses keep multi-segment images from wrapping. */
			for (long i = 0; i < image_len; i++) {
				mem_w8(c, base + (uint32_t)i, file[header_len + i]);
			}
			for (int i = 0; i < reloc_n; i++) {
				long e = (long)reloc_off + i * 4;
				uint32_t at = base + (uint32_t)le16(file + e + 2) * 16 + le16(file + e);
				mem_w16(c, at, (uint16_t)(mem_r16(c, at) + load_seg));
			}
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
			return 11;
		}
		if (minimum) *minimum = 0x1000;
		if (maximum) *maximum = 0xFFFF;
		if (paras < 0x1000) { free(file); return 8; }
		load_seg = psp;
		for (long i = 0; c && i < len; i++) {
			wr8(c, load_seg, (uint16_t)(0x100 + i), file[i]);
		}
		start_cs = load_seg;
		start_ip = 0x100;
		start_ss = load_seg;
		start_sp = STACK_TOP;
		if (c) wr16(c, psp, STACK_TOP, 0);
	}
	free(file);
	if (!c) return 0;

	/* The PSP, at the segment just before the program. DS and ES point
	 * at the PSP, as DOS starts a program; for a .COM that is also the
	 * program's own segment (load_seg was set to it above). */
	c->sreg[1] = start_cs;
	c->ip = start_ip;
	c->sreg[2] = start_ss;
	c->r[SP] = start_sp;
	c->sreg[3] = psp;
	c->sreg[0] = psp;
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

int load_program(struct cpu *c, const char *path, const char *tail,
                 char *err, unsigned long errlen)
{
	(void)tail;
	return load_program_at(c, path, PSP_SEG, 0xA000 - PSP_SEG,
	                       NULL, NULL, err, errlen) ? -1 : 0;
}
