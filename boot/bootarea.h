/* The boot area (M14, ADR-0006): what the ManiOS boot loader boots, the
 * same bytes on a CD (a file, MANIOS.BIN) and on a hard disk (partition
 * type 0xDA). Every part starts on a 2048-byte boundary, so it can be
 * read in CD sectors as well as disk sectors:
 *
 *   0      this header (512 bytes used)
 *   2048   the MBR boot code (mbr.bin), for the installer to write
 *   ...    stage 2 (stage2.bin), loaded at STAGE2_ADDR
 *   ...    the kernel, an ELF file (Multiboot 1)
 *
 * All numbers are little-endian. Included by assembly (mbr.S,
 * cdboot.S, stage2_entry.S: the offsets and addresses) and by C
 * (stage 2, the installer). */
#ifndef MANIOS_BOOTAREA_H
#define MANIOS_BOOTAREA_H

#define BOOTAREA_MAGIC "ZKTBOOT1"
#define BOOTAREA_VERSION 2 /* 2 (0.17.1): with BA_LOAD_ADDR */
#define BOOTAREA_ALIGN 2048
#define BOOTAREA_PARTITION_TYPE 0xDA /* "non-filesystem data" */

/* Header offsets. */
#define BA_MAGIC       0   /* 8 bytes */
#define BA_VERSION     8
#define BA_TOTAL       12  /* bytes in the whole boot area */
#define BA_MBR_OFF     16
#define BA_MBR_LEN     20
#define BA_STAGE2_OFF  24
#define BA_STAGE2_LEN  28
#define BA_KERNEL_OFF  32
#define BA_KERNEL_LEN  36
#define BA_CRC32       40  /* of bytes [BOOTAREA_ALIGN, total): all but the header */
#define BA_OS_VERSION  48  /* 16 bytes, NUL-padded: "0.14.0" */
#define BA_LOAD_ADDR   64  /* where stage 2 puts the whole boot area: just past the
                            * kernel's memory and its frame bitmap (mkbootarea.py) */
#define BA_CMDLINE     256 /* 256 bytes, NUL-terminated: the kernel command line */
#define BA_CMDLINE_MAX 256

/* Where the loaders put things (physical addresses, all below 1 MiB
 * but the last). */
#define HEADER_ADDR  0x1000  /* the header, read by the first stage */
#define CMDLINE_ADDR 0x1800  /* the command line being booted */
#define MMAP_ADDR    0x2000  /* the BIOS memory map, as Multiboot entries */
#define MMAP_MAX     0x0F00
#define MBI_ADDR     0x3000  /* the Multiboot information for the kernel */
#define STACK_TOP    0x7000
#define STAGE2_ADDR  0x8000  /* up to 0x1FFFF */
#define BOUNCE_SEG   0x2000  /* 0x20000: disk reads land here first */
#define BOUNCE_SIZE  0x8000
/* The whole boot area goes at header[BA_LOAD_ADDR], above the kernel,
 * which gets it as a Multiboot module. Until 0.17.1 that was a fixed
 * 4 MiB, which capped the kernel at 3 MiB. */
#define AREA_MIN_ADDR 0x100000
#define AREA_MAX_SIZE 0x1000000 /* 16 MiB: the kernel's modules window */
/* The memory ManiOS needs: its images, and this much more to run in. */
#define RUN_MEMORY    0x200000

#ifndef __ASSEMBLER__
#include <stdint.h>

static inline uint32_t bootarea_u32(const void *p, unsigned offset)
{
	const uint8_t *b = (const uint8_t *)p + offset;
	return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

/* CRC-32 (IEEE 802.3, as zlib's), bit by bit: no table to carry. */
static inline uint32_t bootarea_crc32(uint32_t crc, const uint8_t *p, uint32_t len)
{
	crc = ~crc;
	while (len--) {
		crc ^= *p++;
		for (int k = 0; k < 8; k++) {
			crc = crc >> 1 ^ (0xEDB88320u & -(crc & 1));
		}
	}
	return ~crc;
}
#endif

#endif
