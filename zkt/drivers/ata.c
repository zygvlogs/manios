/* ATA disks over PIO on the legacy IDE ports. Written from the public
 * ATA/ATAPI specifications (T13), register-level; polled, with the
 * device's interrupt disabled (nIEN), which works on every IDE
 * controller back to the ISA era. */
#include "ata.h"
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

#define CMD_READ_SECTORS 0x20
#define CMD_IDENTIFY     0xEC

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

static bool identify(struct ata_channel *ch, bool slave, uint16_t *id)
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
		return false; /* no drive, or no controller (floating bus) */
	}
	if (wait_not_busy(ch) < 0) {
		return false;
	}
	/* ATAPI and SATA devices abort IDENTIFY and leave a signature here. */
	if (inb(ch->io + REG_LBA1) || inb(ch->io + REG_LBA2)) {
		return false;
	}
	int st = wait_data(ch);
	if (st < 0 || (st & (STATUS_ERR | STATUS_DF))) {
		return false;
	}
	for (int i = 0; i < 256; i++) {
		id[i] = inw(ch->io + REG_DATA);
	}
	return true;
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

static int ata_read(struct device *dev, uint32_t lba, uint32_t count, void *buf)
{
	struct ata_drive *d = dev->driver_data;
	return read_sectors(d, lba, count, buf, !d->lba);
}

static const struct block_device_ops ata_ops = { .read = ata_read };

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

static void probe(struct ata_channel *ch, bool slave, int index)
{
	uint16_t *id = kmalloc(512);
	if (!id || !identify(ch, slave, id)) {
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
