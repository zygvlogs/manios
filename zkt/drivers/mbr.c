#include "mbr.h"
#include <stdbool.h>
#include <stdint.h>
#include "heap.h"
#include "kprintf.h"
#include "kstring.h"

#define TABLE_OFFSET 446
#define ENTRY_SIZE 16
#define ENTRY_STATUS 0
#define ENTRY_TYPE 4
#define ENTRY_START 8
#define ENTRY_SECTORS 12

#define STATUS_INACTIVE 0x00
#define STATUS_ACTIVE 0x80

struct partition {
	struct device dev;
	struct device *disk;
	uint32_t start;
};

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool is_extended(uint8_t type)
{
	return type == 0x05 || type == 0x0F || type == 0x85;
}

static int partition_read(struct device *dev, uint32_t lba, uint32_t count, void *buf)
{
	struct partition *p = dev->driver_data;
	return device_read_blocks(p->disk, p->start + lba, count, buf);
}

static int partition_write(struct device *dev, uint32_t lba, uint32_t count, const void *buf)
{
	struct partition *p = dev->driver_data;
	return device_write_blocks(p->disk, p->start + lba, count, buf);
}

static const struct block_device_ops partition_ops = { .read = partition_read,
	                                                   .write = partition_write };

/* A FAT boot sector on a partitionless disk ("superfloppy") ends in
 * 55 AA too, and its bytes where partition entries would be can be
 * anything, including all zeros. A plausible BIOS Parameter Block means
 * the sector is a filesystem's boot sector: an MBR's boot code occupies
 * those bytes. */
static bool has_fat_bpb(const uint8_t *s)
{
	uint16_t bytes_per_sector = (uint16_t)(s[11] | s[12] << 8);
	uint8_t per_cluster = s[13];
	uint16_t reserved = (uint16_t)(s[14] | s[15] << 8);
	uint8_t fats = s[16];
	uint8_t media = s[21];

	return bytes_per_sector == 512 && per_cluster && !(per_cluster & (per_cluster - 1))
	       && reserved >= 1 && (fats == 1 || fats == 2)
	       && (media == 0xF0 || media >= 0xF8);
}

static bool looks_like_mbr(const uint8_t *sector)
{
	if (sector[510] != 0x55 || sector[511] != 0xAA || has_fat_bpb(sector)) {
		return false;
	}
	for (int i = 0; i < 4; i++) {
		uint8_t status = sector[TABLE_OFFSET + i * ENTRY_SIZE + ENTRY_STATUS];
		if (status != STATUS_INACTIVE && status != STATUS_ACTIVE) {
			return false;
		}
	}
	return true;
}

void mbr_scan(struct device *disk)
{
	uint8_t *sector = kmalloc(512);
	if (!sector || disk->block_size != 512
	    || device_read_blocks(disk, 0, 1, sector) != 0 || !looks_like_mbr(sector)) {
		kfree(sector);
		return;
	}

	for (int i = 0; i < 4; i++) {
		const uint8_t *e = sector + TABLE_OFFSET + i * ENTRY_SIZE;
		uint8_t type = e[ENTRY_TYPE];
		uint32_t start = le32(e + ENTRY_START);
		uint32_t count = le32(e + ENTRY_SECTORS);

		if (!type || !count || is_extended(type) || start == 0
		    || start >= disk->block_count || count > disk->block_count - start) {
			continue;
		}
		struct partition *p = kmalloc(sizeof(*p));
		if (!p) {
			break;
		}
		memset(p, 0, sizeof(*p));
		p->disk = disk;
		p->start = start;
		ksnprintf(p->dev.name, sizeof(p->dev.name), "%sp%d", disk->name, i + 1);
		p->dev.class = DEVICE_BLOCK;
		p->dev.block_ops = &partition_ops;
		p->dev.block_size = 512;
		p->dev.block_count = count;
		p->dev.driver_data = p;
		if (device_register(&p->dev) != 0) {
			kfree(p);
			continue;
		}
		kprintf("%s: type 0x%02x, sectors %lu-%lu\n", p->dev.name, type, start,
		        start + count - 1);
	}
	kfree(sector);
}
