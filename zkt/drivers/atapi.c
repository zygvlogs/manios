/* CD and DVD drives (atapi.h), written from T10's SCSI Multimedia
 * Commands (MMC) and SCSI Primary Commands: TEST UNIT READY (is a disc
 * in?), READ CAPACITY (its size), READ(10), and REQUEST SENSE after an
 * error -- which SCSI requires: until the sense data is read, a drive
 * answers every command with the same error. UNIT ATTENTION says the
 * disc changed (or the drive was reset), NOT READY that there is none. */
#include "atapi.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"

#define SENSE_NOT_READY      2
#define SENSE_UNIT_ATTENTION 6
#define ERROR(key) (-(0x100 + (key)))

static int cd_count;

static long command(struct atapi *a, const uint8_t cdb[12], void *buf, uint32_t len)
{
	long rc = a->packet(a, cdb, buf, len);
	if (rc <= -0x100) {
		uint8_t request_sense[12] = { 0x03, 0, 0, 0, 18 }, sense[18];
		if (a->packet(a, request_sense, sense, sizeof(sense)) >= 3 && (sense[0] & 0x7E) == 0x70) {
			rc = ERROR(sense[2] & 0x0F);
		}
	}
	return rc;
}

/* Asks the disc's size (READ CAPACITY): true if there is a readable one. */
static bool measure(struct atapi *a)
{
	uint8_t ready[12] = { 0x00 }; /* TEST UNIT READY */
	uint8_t capacity[12] = { 0x25 };
	uint8_t answer[8];
	a->measured = false;
	a->dev.block_count = 0;
	/* The first answers after power-on or a new disc are UNIT ATTENTION. */
	for (int i = 0; i < 3; i++) {
		long rc = command(a, ready, 0, 0);
		if (rc == 0) {
			break;
		}
		if (rc != ERROR(SENSE_UNIT_ATTENTION)) {
			return false;
		}
	}
	if (command(a, capacity, answer, sizeof(answer)) != 8) {
		return false;
	}
	uint32_t last = (uint32_t)answer[0] << 24 | (uint32_t)answer[1] << 16
	                | (uint32_t)answer[2] << 8 | answer[3];
	a->dev.block_count = last + 1;
	a->measured = true;
	return true;
}

/* The disc is gone. */
static void lost(struct atapi *a)
{
	if (a->measured) {
		a->measured = false;
		a->dev.block_count = 0;
		a->dev.media_changes++;
	}
}

static int cd_read(struct device *dev, uint32_t lba, uint32_t count, void *buf)
{
	struct atapi *a = dev->driver_data;
	uint8_t *out = buf;
	int rc = 0;

	mutex_lock(a->lock);
	while (count && rc == 0) {
		uint32_t n = count < CD_MAX_BLOCKS_PER_COMMAND ? count : CD_MAX_BLOCKS_PER_COMMAND;
		uint8_t cdb[12] = {
			0x28, 0, (uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8),
			(uint8_t)lba, 0, (uint8_t)(n >> 8), (uint8_t)n, 0, 0, 0,
		};
		long got = a->measured ? command(a, cdb, out, n * CD_BLOCK) : ERROR(SENSE_UNIT_ATTENTION);
		if (got == ERROR(SENSE_UNIT_ATTENTION)) {
			/* A new disc (or none): measured again, and this read fails --
			 * it was meant for the old one. */
			measure(a);
			dev->media_changes++;
			rc = -EIO;
		} else if (got == ERROR(SENSE_NOT_READY)) {
			lost(a);
			rc = -ENXIO;
		} else if (got != (long)(n * CD_BLOCK)) {
			rc = -EIO;
		}
		lba += n;
		count -= n;
		out += n * CD_BLOCK;
	}
	mutex_unlock(a->lock);
	return rc;
}

/* TEST UNIT READY says whether a disc is in and, once, that it is a
 * new one. */
static void cd_check_media(struct device *dev)
{
	struct atapi *a = dev->driver_data;
	uint8_t ready[12] = { 0x00 };
	mutex_lock(a->lock);
	long rc = command(a, ready, 0, 0);
	if (rc == ERROR(SENSE_UNIT_ATTENTION) || (rc == 0 && !a->measured)) {
		measure(a);
		dev->media_changes++;
	} else if (rc != 0) {
		lost(a);
	}
	mutex_unlock(a->lock);
}

static const struct block_device_ops cd_ops = { .read = cd_read, .check_media = cd_check_media };

int atapi_register(struct atapi *a, const char *how)
{
	ksnprintf(a->dev.name, sizeof(a->dev.name), "cd%d", cd_count);
	a->dev.class = DEVICE_BLOCK;
	a->dev.block_ops = &cd_ops;
	a->dev.block_size = CD_BLOCK;
	a->dev.driver_data = a;
	mutex_lock(a->lock);
	bool disc = measure(a);
	mutex_unlock(a->lock);
	int rc = device_register(&a->dev);
	if (rc != 0) {
		return rc;
	}
	cd_count++;
	if (disc) {
		kprintf("%s: %s, %s, disc of %lu MiB\n", a->dev.name, a->model, how,
		        (a->dev.block_count + 511) / 512);
	} else {
		kprintf("%s: %s, %s, no disc\n", a->dev.name, a->model, how);
	}
	return 0;
}
