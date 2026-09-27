/* ATA disks over PIO on the legacy IDE ports. Written from the public
 * ATA/ATAPI specifications (T13), register-level; polled, with the
 * device's interrupt disabled (nIEN), which works on every IDE
 * controller back to the ISA era.
 *
 * CD and DVD drives on the same ports speak ATAPI: SCSI commands sent
 * as a 12-byte packet through the PACKET command, their data coming
 * back a 2048-byte block per DRQ. This file is that transport; what the
 * commands are, and the drives' devices, are atapi.c's. */
#include "ata.h"
#include "atapi.h"
#include <stdbool.h>
#include <stdint.h>
#include "device.h"
#include "heap.h"
#include "io.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "mbr.h"
#include "mutex.h"
#include "timer.h"

#define REG_DATA     0
#define REG_ERROR    1
#define REG_SECCOUNT 2
#define REG_LBA0     3 /* CHS: sector number */
#define REG_LBA1     4 /* CHS: cylinder low */
#define REG_LBA2     5 /* CHS: cylinder high */
#define REG_DRIVE    6
#define REG_STATUS   7 /* read */
#define REG_COMMAND  7 /* write */

#define STATUS_ERR  0x01
#define STATUS_DRQ  0x08
#define STATUS_DF   0x20
#define STATUS_BSY  0x80

#define DRIVE_BASE  0xA0 /* bits 7 and 5 are always set */
#define DRIVE_LBA   0x40
#define DRIVE_SLAVE 0x10

#define DEVCTL_NIEN 0x02 /* no interrupts: this driver polls */

#define CMD_READ_SECTORS  0x20
#define CMD_WRITE_SECTORS 0x30
#define CMD_CACHE_FLUSH   0xE7 /* ATA-4 on; older drives abort it, harmlessly */
#define CMD_IDENTIFY      0xEC
#define CMD_PACKET        0xA0
#define CMD_IDENTIFY_PACKET 0xA1

/* A PACKET device's signature, left where IDENTIFY DEVICE aborted. */
#define ATAPI_SIG1 0x14
#define ATAPI_SIG2 0xEB
#define CD_TIMEOUT_MS 10000 /* a disc may have to spin up */

#define SECTOR_SIZE 512
#define MAX_SECTORS_PER_COMMAND 256
#define TIMEOUT_MS 5000

/* IDENTIFY DEVICE words (ATA-1 onward). */
#define ID_DEFAULT_CYLINDERS 1
#define ID_DEFAULT_HEADS     3
#define ID_DEFAULT_SECTORS   6
#define ID_MODEL             27 /* 20 words, two characters each */
#define ID_CAPABILITIES      49
#define ID_FIELD_VALIDITY    53
#define ID_CURRENT_CYLINDERS 54
#define ID_CURRENT_HEADS     55
#define ID_CURRENT_SECTORS   56
#define ID_LBA28_SECTORS     60 /* two words, low first */

#define CAP_LBA           (1u << 9)
#define VALID_CURRENT_CHS (1u << 0)

struct ata_channel {
	uint16_t io;
	uint16_t ctrl;
	struct mutex lock; /* one command at a time per channel */
};

struct ata_drive {
	struct device dev;
	struct ata_channel *channel;
	bool slave;
	bool lba;
	uint16_t cylinders, heads, sectors; /* CHS geometry the drive is using */
	char model[41];
};

static struct ata_channel channels[2] = {
	{ 0x1F0, 0x3F6, MUTEX_INIT },
	{ 0x170, 0x376, MUTEX_INIT },
};

/* Reading the alternate status register takes ~100 ns on ISA timing;
 * four reads give the 400 ns the spec requires after selecting a drive
 * or issuing a command, before status is valid. */
static void delay_400ns(struct ata_channel *ch)
{
	for (int i = 0; i < 4; i++) {
		inb(ch->ctrl);
	}
}

/* Returns the status once BSY clears, or -1 on timeout. */
static int wait_not_busy(struct ata_channel *ch)
{
	uint64_t deadline = timer_uptime_ms() + TIMEOUT_MS;
	for (;;) {
		uint8_t status = inb(ch->io + REG_STATUS);
		if (!(status & STATUS_BSY)) {
			return status;
		}
		if (timer_uptime_ms() > deadline) {
			return -1;
		}
	}
}

/* Returns the status once the drive has data ready (DRQ) or has failed
 * (ERR/DF), or -1 on timeout. */
static int wait_data(struct ata_channel *ch)
{
	uint64_t deadline = timer_uptime_ms() + TIMEOUT_MS;
	for (;;) {
		uint8_t status = inb(ch->io + REG_STATUS);
		if (!(status & STATUS_BSY) && (status & (STATUS_DRQ | STATUS_ERR | STATUS_DF))) {
			return status;
		}
		if (timer_uptime_ms() > deadline) {
			return -1;
		}
	}
}

enum kind { NONE, ATA, ATAPI };

static enum kind identify(struct ata_channel *ch, bool slave, uint16_t *id)
{
	outb(ch->ctrl, DEVCTL_NIEN);
	outb(ch->io + REG_DRIVE, DRIVE_BASE | (slave ? DRIVE_SLAVE : 0));
	delay_400ns(ch);
	outb(ch->io + REG_SECCOUNT, 0);
	outb(ch->io + REG_LBA0, 0);
	outb(ch->io + REG_LBA1, 0);
	outb(ch->io + REG_LBA2, 0);
	outb(ch->io + REG_COMMAND, CMD_IDENTIFY);
	delay_400ns(ch);

	uint8_t status = inb(ch->io + REG_STATUS);
	if (status == 0 || status == 0xFF) {
		return NONE; /* no drive, or no controller (floating bus) */
	}
	if (wait_not_busy(ch) < 0) {
		return NONE;
	}
	/* ATAPI and SATA devices abort IDENTIFY and leave a signature here. */
	enum kind kind = ATA;
	uint8_t sig1 = inb(ch->io + REG_LBA1), sig2 = inb(ch->io + REG_LBA2);
	if (sig1 == ATAPI_SIG1 && sig2 == ATAPI_SIG2) {
		kind = ATAPI;
		outb(ch->io + REG_COMMAND, CMD_IDENTIFY_PACKET);
		delay_400ns(ch);
	} else if (sig1 || sig2) {
		return NONE;
	}
	int st = wait_data(ch);
	if (st < 0 || (st & (STATUS_ERR | STATUS_DF))) {
		return NONE;
	}
	for (int i = 0; i < 256; i++) {
		id[i] = inw(ch->io + REG_DATA);
	}
	return kind;
}

/* Programs the address registers for one command. */
static void set_address(struct ata_drive *d, uint32_t lba, uint32_t count, bool chs)
{
	struct ata_channel *ch = d->channel;
	uint8_t drive = DRIVE_BASE | (d->slave ? DRIVE_SLAVE : 0);

	if (chs) {
		uint32_t per_cylinder = (uint32_t)d->heads * d->sectors;
		uint32_t cylinder = lba / per_cylinder;
		uint32_t in_cylinder = lba % per_cylinder;
		outb(ch->io + REG_DRIVE, drive | (uint8_t)(in_cylinder / d->sectors));
		delay_400ns(ch);
		outb(ch->io + REG_LBA0, (uint8_t)(in_cylinder % d->sectors + 1));
		outb(ch->io + REG_LBA1, (uint8_t)(cylinder & 0xFF));
		outb(ch->io + REG_LBA2, (uint8_t)(cylinder >> 8));
	} else {
		outb(ch->io + REG_DRIVE, drive | DRIVE_LBA | (uint8_t)((lba >> 24) & 0x0F));
		delay_400ns(ch);
		outb(ch->io + REG_LBA0, (uint8_t)(lba & 0xFF));
		outb(ch->io + REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
		outb(ch->io + REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
	}
	outb(ch->io + REG_SECCOUNT, (uint8_t)(count & 0xFF)); /* 0 means 256 */
}

static int read_sectors(struct ata_drive *d, uint32_t lba, uint32_t count, void *buf, bool chs)
{
	struct ata_channel *ch = d->channel;
	uint16_t *out = buf;
	int rc = 0;

	mutex_lock(&ch->lock);
	while (count && rc == 0) {
		uint32_t n = count < MAX_SECTORS_PER_COMMAND ? count : MAX_SECTORS_PER_COMMAND;
		set_address(d, lba, n, chs);
		outb(ch->io + REG_COMMAND, CMD_READ_SECTORS);

		for (uint32_t s = 0; s < n; s++) {
			delay_400ns(ch);
			int st = wait_data(ch);
			if (st < 0 || (st & (STATUS_ERR | STATUS_DF))) {
				rc = -EIO;
				break;
			}
			for (int w = 0; w < SECTOR_SIZE / 2; w++) {
				*out++ = inw(ch->io + REG_DATA);
			}
		}
		lba += n;
		count -= n;
	}
	mutex_unlock(&ch->lock);
	return rc;
}

/* PIO writes (M14, for the installer): each sector goes when the drive
 * asks for it (DRQ); then the drive's write cache is flushed, so what
 * was written survives the power going off. */
static int write_sectors(struct ata_drive *d, uint32_t lba, uint32_t count, const void *buf,
                         bool chs)
{
	struct ata_channel *ch = d->channel;
	const uint16_t *in = buf;
	int rc = 0;

	mutex_lock(&ch->lock);
	while (count && rc == 0) {
		uint32_t n = count < MAX_SECTORS_PER_COMMAND ? count : MAX_SECTORS_PER_COMMAND;
		set_address(d, lba, n, chs);
		outb(ch->io + REG_COMMAND, CMD_WRITE_SECTORS);
		for (uint32_t s = 0; s < n; s++) {
			delay_400ns(ch);
			int st = wait_data(ch);
			if (st < 0 || (st & (STATUS_ERR | STATUS_DF)) || !(st & STATUS_DRQ)) {
				rc = -EIO;
				break;
			}
			for (int w = 0; w < SECTOR_SIZE / 2; w++) {
				outw(ch->io + REG_DATA, *in++);
			}
		}
		/* The last sector is on its way once the drive is not busy. */
		delay_400ns(ch);
		if (rc == 0 && wait_not_busy(ch) < 0) {
			rc = -EIO;
		}
		if (rc == 0 && (inb(ch->io + REG_STATUS) & (STATUS_ERR | STATUS_DF))) {
			rc = -EIO;
		}
		lba += n;
		count -= n;
	}
	if (rc == 0) {
		outb(ch->io + REG_DRIVE, DRIVE_BASE | (d->slave ? DRIVE_SLAVE : 0));
		outb(ch->io + REG_COMMAND, CMD_CACHE_FLUSH);
		delay_400ns(ch);
		if (wait_not_busy(ch) < 0) {
			rc = -EIO;
		}
	}
	mutex_unlock(&ch->lock);
	return rc;
}

static int ata_read(struct device *dev, uint32_t lba, uint32_t count, void *buf)
{
	struct ata_drive *d = dev->driver_data;
	return read_sectors(d, lba, count, buf, !d->lba);
}

static int ata_write(struct device *dev, uint32_t lba, uint32_t count, const void *buf)
{
	struct ata_drive *d = dev->driver_data;
	return write_sectors(d, lba, count, buf, !d->lba);
}

static const struct block_device_ops ata_ops = { .read = ata_read, .write = ata_write };

/* Model strings store two characters per word, high byte first. */
static void copy_model(char *dst, const uint16_t *id)
{
	for (int i = 0; i < 20; i++) {
		dst[2 * i] = (char)(id[ID_MODEL + i] >> 8);
		dst[2 * i + 1] = (char)(id[ID_MODEL + i] & 0xFF);
	}
	int len = 40;
	while (len > 0 && dst[len - 1] == ' ') {
		len--;
	}
	dst[len] = '\0';
}

/* Pre-LBA drives can only be addressed by CHS, so every LBA-capable
 * drive also gets its CHS addressing checked against LBA reads, across
 * a head boundary and a cylinder boundary. That exercises the same
 * arithmetic a CHS-only drive relies on. */
static bool chs_matches_lba(struct ata_drive *d)
{
	uint32_t per_cylinder = (uint32_t)d->heads * d->sectors;
	uint32_t starts[2] = { d->sectors - 2u, per_cylinder - 2u };
	uint32_t chs_capacity = per_cylinder * d->cylinders;
	uint8_t *a = kmalloc(4 * SECTOR_SIZE);
	uint8_t *b = kmalloc(4 * SECTOR_SIZE);
	bool ok = a && b;

	for (int i = 0; i < 2 && ok; i++) {
		if (starts[i] + 4 > chs_capacity || starts[i] + 4 > d->dev.block_count) {
			continue;
		}
		ok = read_sectors(d, starts[i], 4, a, false) == 0
		     && read_sectors(d, starts[i], 4, b, true) == 0
		     && memcmp(a, b, 4 * SECTOR_SIZE) == 0;
	}
	kfree(a);
	kfree(b);
	return ok;
}

static void atapi_probe(struct ata_channel *ch, bool slave, const uint16_t *id);

static void probe(struct ata_channel *ch, bool slave, int index)
{
	uint16_t *id = kmalloc(512);
	enum kind kind = id ? identify(ch, slave, id) : NONE;
	if (kind == ATAPI) {
		atapi_probe(ch, slave, id);
	}
	if (kind != ATA) {
		kfree(id);
		return;
	}

	struct ata_drive *d = kmalloc(sizeof(*d));
	if (!d) {
		kfree(id);
		return;
	}
	memset(d, 0, sizeof(*d));
	d->channel = ch;
	d->slave = slave;
	d->lba = id[ID_CAPABILITIES] & CAP_LBA;
	if ((id[ID_FIELD_VALIDITY] & VALID_CURRENT_CHS) && id[ID_CURRENT_SECTORS]) {
		d->cylinders = id[ID_CURRENT_CYLINDERS];
		d->heads = id[ID_CURRENT_HEADS];
		d->sectors = id[ID_CURRENT_SECTORS];
	} else {
		d->cylinders = id[ID_DEFAULT_CYLINDERS];
		d->heads = id[ID_DEFAULT_HEADS];
		d->sectors = id[ID_DEFAULT_SECTORS];
	}
	copy_model(d->model, id);

	ksnprintf(d->dev.name, sizeof(d->dev.name), "ata%d", index);
	d->dev.class = DEVICE_BLOCK;
	d->dev.block_ops = &ata_ops;
	d->dev.block_size = SECTOR_SIZE;
	d->dev.block_count = d->lba
	    ? (uint32_t)id[ID_LBA28_SECTORS] | (uint32_t)id[ID_LBA28_SECTORS + 1] << 16
	    : (uint32_t)d->cylinders * d->heads * d->sectors;
	d->dev.driver_data = d;
	kfree(id);

	if (d->dev.block_count == 0 || d->heads == 0 || d->sectors == 0
	    || device_register(&d->dev) != 0) {
		kfree(d);
		return;
	}

	kprintf("%s: %s, %lu MiB, %s, CHS %u/%u/%u\n", d->dev.name, d->model,
	        d->dev.block_count / 2048, d->lba ? "LBA" : "CHS only",
	        d->cylinders, d->heads, d->sectors);
	if (d->lba) {
		kprintf("%s: CHS cross-check %s\n", d->dev.name,
		        chs_matches_lba(d) ? "passed" : "FAILED");
	}
	mbr_scan(&d->dev);
}

struct atapi_drive {
	struct atapi a; /* first: the transport's calls get this */
	struct ata_channel *channel;
	bool slave;
};

/* The ATAPI transport over the IDE ports (atapi.h): the PACKET
 * command, then the packet, then the data a DRQ block at a time (up to
 * `len` bytes kept, the rest read and dropped). */
static long packet(struct atapi *a, const uint8_t cdb[12], void *buf, uint32_t len)
{
	struct atapi_drive *d = (struct atapi_drive *)a;
	struct ata_channel *ch = d->channel;
	uint8_t *out = buf;
	uint32_t done = 0;

	outb(ch->io + REG_DRIVE, DRIVE_BASE | (d->slave ? DRIVE_SLAVE : 0));
	delay_400ns(ch);
	if (wait_not_busy(ch) < 0) {
		return -EIO;
	}
	outb(ch->io + REG_ERROR, 0);    /* features: PIO, not DMA */
	outb(ch->io + REG_LBA1, CD_BLOCK & 0xFF); /* bytes per DRQ, at most */
	outb(ch->io + REG_LBA2, CD_BLOCK >> 8);
	outb(ch->io + REG_COMMAND, CMD_PACKET);
	delay_400ns(ch);
	int st = wait_data(ch);
	if (st < 0 || !(st & STATUS_DRQ)) {
		return st >= 0 && (st & STATUS_ERR) ? -(0x100 + (inb(ch->io + REG_ERROR) >> 4)) : -EIO;
	}
	for (int i = 0; i < 6; i++) {
		outw(ch->io + REG_DATA, (uint16_t)(cdb[2 * i] | cdb[2 * i + 1] << 8));
	}
	uint64_t deadline = timer_uptime_ms() + CD_TIMEOUT_MS;
	for (;;) {
		delay_400ns(ch);
		st = inb(ch->io + REG_STATUS);
		if (st & STATUS_BSY) {
			if (timer_uptime_ms() > deadline) {
				return -EIO;
			}
			continue;
		}
		if (st & (STATUS_ERR | STATUS_DF)) {
			return -(0x100 + (inb(ch->io + REG_ERROR) >> 4));
		}
		if (!(st & STATUS_DRQ)) {
			return (long)done; /* the command is over */
		}
		uint32_t n = inb(ch->io + REG_LBA1) | (uint32_t)inb(ch->io + REG_LBA2) << 8;
		for (uint32_t i = 0; i < (n + 1) / 2; i++) {
			uint16_t w = inw(ch->io + REG_DATA);
			if (done + 2 <= len) {
				out[done] = (uint8_t)w;
				out[done + 1] = (uint8_t)(w >> 8);
			}
			done += 2;
		}
		deadline = timer_uptime_ms() + CD_TIMEOUT_MS;
	}
}

static void atapi_probe(struct ata_channel *ch, bool slave, const uint16_t *id)
{
	/* Word 0, bits 12-8: the device type; 5 is a CD/DVD drive. */
	if (((id[0] >> 8) & 0x1F) != 5) {
		return;
	}
	struct atapi_drive *d = kmalloc(sizeof(*d));
	if (!d) {
		return;
	}
	memset(d, 0, sizeof(*d));
	d->channel = ch;
	d->slave = slave;
	d->a.packet = packet;
	d->a.lock = &ch->lock;
	copy_model(d->a.model, id);
	if (atapi_register(&d->a, "ATAPI") != 0) {
		kfree(d);
	}
}

void ata_init(void)
{
	for (int c = 0; c < 2; c++) {
		if (inb(channels[c].io + REG_STATUS) == 0xFF) {
			continue; /* floating bus: no controller on this channel */
		}
		probe(&channels[c], false, c * 2);
		probe(&channels[c], true, c * 2 + 1);
	}
}
