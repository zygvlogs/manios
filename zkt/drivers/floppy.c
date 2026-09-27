/* Floppy disks, on the PC's floppy disk controller: the Intel 82077AA
 * and the NEC uPD765 it is compatible with, at ports 0x3F0-0x3F7, IRQ 6
 * and ISA DMA channel 2. Written from Intel's 82077AA data sheet and
 * the PC/AT's CMOS layout.
 *
 * The drives are the ones the CMOS lists (register 0x10, which the BIOS
 * keeps). A disk's format is found by trying, for each format the drive
 * can take, the data rate and the last sector of the first cylinder:
 * the first that reads is the format. Transfers go a cylinder at a time
 * at most (both heads, the "multi-track" bit), by DMA through a buffer
 * below 16 MiB; the controller interrupts when one is done. Reads fetch
 * the whole cylinder, and the last few cylinders read are kept: file
 * systems read a block at a time, and a block a revolution is slow --
 * and FAT goes back to the FAT (cylinder 0) between clusters. The motor
 * is left running for a few seconds after a transfer, as the next one
 * usually follows, and a thread turns it off. */
#include "floppy.h"
#include <stdbool.h>
#include <stdint.h>
#include "cpu.h"
#include "device.h"
#include "dma.h"
#include "heap.h"
#include "io.h"
#include "irq.h"
#include "isadma.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "mutex.h"
#include "rtc.h"
#include "sched.h"
#include "timer.h"

#define FDC_DOR  0x3F2 /* digital output: drive select, reset, DMA, motors */
#define FDC_MSR  0x3F4 /* main status (read) */
#define FDC_FIFO 0x3F5 /* commands, results */
#define FDC_DIR  0x3F7 /* digital input (read): disk changed */
#define FDC_CCR  0x3F7 /* configuration control (write): data rate */

#define DOR_NRESET   0x04
#define DOR_DMA      0x08 /* interrupts and DMA on */
#define DOR_MOTOR(u) (0x10 << (u))
#define MSR_RQM 0x80 /* ready for a byte */
#define MSR_DIO 0x40 /* ... from the controller */
#define MSR_CB  0x10 /* a command is in progress */
#define DIR_CHANGED 0x80

#define CMD_SPECIFY     0x03
#define CMD_WRITE       0x05
#define CMD_READ        0x06
#define CMD_RECALIBRATE 0x07
#define CMD_SENSE       0x08
#define CMD_SEEK        0x0F
#define CMD_VERSION     0x10
#define CMD_MT          0x80 /* on to the other head at the track's end */
#define CMD_MFM         0x40 /* double density */

#define ST0_CODE  0xC0 /* 00: ended normally */
#define ST0_SEEK  0x20
#define ST1_WRITE_PROTECTED 0x02

#define IRQ 6
#define DMA_CHANNEL 2
#define SECTOR 512
#define SIZE_CODE 2 /* 128 << 2 = 512 */
#define MAX_SECTORS 36
#define BUFFER_BYTES (2 * MAX_SECTORS * SECTOR) /* a cylinder */
#define MOTOR_IDLE_MS 3000
#define SPIN_UP_MS 500
#define RETRIES 3

#define RATE_500K 0
#define RATE_300K 1
#define RATE_250K 2
#define RATE_1M   3

struct format {
	const char *name;
	uint8_t cylinders, heads, sectors, rate, gap;
};

static const struct format F360 = { "360 KB", 40, 2, 9, RATE_250K, 0x2A };
static const struct format F720 = { "720 KB", 80, 2, 9, RATE_250K, 0x1B };
static const struct format F1200 = { "1.2 MB", 80, 2, 15, RATE_500K, 0x1B };
static const struct format F1440 = { "1.44 MB", 80, 2, 18, RATE_500K, 0x1B };
static const struct format F2880 = { "2.88 MB", 80, 2, 36, RATE_1M, 0x1B };

/* CMOS drive types (the PC/AT's), and the formats each takes, the
 * drive's own first. */
static const struct {
	const char *name;
	const struct format *formats[3];
} TYPES[] = {
	[1] = { "5.25\" 360 KB", { &F360 } },
	[2] = { "5.25\" 1.2 MB", { &F1200 } },
	[3] = { "3.5\" 720 KB", { &F720 } },
	[4] = { "3.5\" 1.44 MB", { &F1440, &F720 } },
	[5] = { "3.5\" 2.88 MB", { &F2880, &F1440, &F720 } },
};
#define TYPE_COUNT (int)(sizeof(TYPES) / sizeof(TYPES[0]))

struct drive {
	struct device dev;
	int unit, type;
	const struct format *format; /* NULL: no disk, or not readable */
	bool calibrated;
	int cylinder;                /* where the head is, if calibrated */
};

static struct drive drives[2];
static struct mutex lock = MUTEX_INIT; /* the controller: one command at a time */
static uint8_t dor;                    /* what DOR holds */
static uint64_t last_used;             /* ms, for the motor */
static uint8_t *buffer;
static uintptr_t buffer_phys;

/* The cylinder cache: the least recently used goes. */
#define CACHE_SLOTS 3
static struct slot {
	struct drive *drive; /* NULL: empty */
	int cylinder;
	uint32_t used;
	uint8_t *data;
} cache[CACHE_SLOTS];
static uint32_t cache_clock;
static volatile bool interrupted;
static struct waitq irq_wait = WAITQ_INIT;

static void fdc_irq(void)
{
	interrupted = true;
	waitq_wake_all(&irq_wait);
}

static bool wait_irq(uint32_t ms)
{
	uint64_t deadline = timer_ticks() + (uint64_t)ms * TIMER_HZ / 1000 + 1;
	uint32_t flags = cpu_irq_save();
	while (!interrupted && timer_ticks() < deadline) {
		waitq_sleep_until(&irq_wait, deadline);
	}
	bool seen = interrupted;
	interrupted = false;
	cpu_irq_restore(flags);
	return seen;
}

static bool send(uint8_t byte)
{
	uint64_t deadline = timer_uptime_ms() + 500;
	while ((inb(FDC_MSR) & (MSR_RQM | MSR_DIO)) != MSR_RQM) {
		if (timer_uptime_ms() > deadline) {
			return false;
		}
	}
	outb(FDC_FIFO, byte);
	return true;
}

static bool send_all(const uint8_t *bytes, int n)
{
	interrupted = false;
	for (int i = 0; i < n; i++) {
		if (!send(bytes[i])) {
			return false;
		}
	}
	return true;
}

/* A command's result bytes: how many came, or -1. */
static int result(uint8_t *out, int max)
{
	int n = 0;
	uint64_t deadline = timer_uptime_ms() + 500;
	for (;;) {
		uint8_t msr = inb(FDC_MSR);
		if ((msr & (MSR_RQM | MSR_DIO)) == (MSR_RQM | MSR_DIO)) {
			uint8_t b = inb(FDC_FIFO);
			if (n < max) {
				out[n] = b;
			}
			n++;
			continue;
		}
		if ((msr & (MSR_RQM | MSR_DIO | MSR_CB)) == MSR_RQM) {
			return n; /* waiting for the next command */
		}
		if (timer_uptime_ms() > deadline) {
			return -1;
		}
	}
}

static bool sense(uint8_t *st0, uint8_t *cylinder)
{
	uint8_t cmd = CMD_SENSE, r[2];
	if (!send_all(&cmd, 1) || result(r, 2) != 2) {
		return false;
	}
	*st0 = r[0];
	*cylinder = r[1];
	return true;
}

static void write_dor(void)
{
	outb(FDC_DOR, dor);
}

/* Resets the controller: afterwards it interrupts once, and each of its
 * four drive positions reports (SENSE INTERRUPT STATUS). */
static bool reset(void)
{
	outb(FDC_DOR, 0);
	interrupted = false;
	timer_sleep_ms(1);
	dor = (uint8_t)((dor & 0xF3) | DOR_NRESET | DOR_DMA);
	write_dor();
	if (!wait_irq(1000)) {
		return false;
	}
	for (int i = 0; i < 4; i++) {
		uint8_t st0, cyl;
		if (!sense(&st0, &cyl)) {
			return false;
		}
	}
	/* Step rate 8 ms, head unload 240 ms, head load 10 ms, DMA. */
	uint8_t specify[3] = { CMD_SPECIFY, 8 << 4 | 0x0F, 5 << 1 };
	drives[0].calibrated = drives[1].calibrated = false;
	return send_all(specify, 3) && result(0, 0) == 0;
}

static void select(struct drive *d)
{
	uint8_t motor = (uint8_t)DOR_MOTOR(d->unit);
	bool spinning = dor & motor;
	dor = (uint8_t)((dor & ~0x03) | motor | d->unit);
	write_dor();
	if (!spinning) {
		timer_sleep_ms(SPIN_UP_MS);
	}
	if (d->format) {
		outb(FDC_CCR, d->format->rate);
	}
}

static bool recalibrate(struct drive *d)
{
	/* The 765 steps at most 77 times, so an 80-cylinder drive may need two. */
	for (int attempt = 0; attempt < 2; attempt++) {
		uint8_t cmd[2] = { CMD_RECALIBRATE, (uint8_t)d->unit }, st0, cyl;
		if (!send_all(cmd, 2) || !wait_irq(3000) || !sense(&st0, &cyl)) {
			return false;
		}
		if ((st0 & (ST0_CODE | ST0_SEEK)) == ST0_SEEK && cyl == 0) {
			d->calibrated = true;
			d->cylinder = 0;
			return true;
		}
	}
	return false;
}

static bool seek(struct drive *d, int cylinder, int head)
{
	if (!d->calibrated && !recalibrate(d)) {
		return false;
	}
	if (d->cylinder == cylinder) {
		return true;
	}
	uint8_t cmd[3] = { CMD_SEEK, (uint8_t)(head << 2 | d->unit), (uint8_t)cylinder }, st0, cyl;
	if (!send_all(cmd, 3) || !wait_irq(3000) || !sense(&st0, &cyl)
	    || (st0 & (ST0_CODE | ST0_SEEK)) != ST0_SEEK || cyl != cylinder) {
		d->calibrated = false;
		return false;
	}
	d->cylinder = cylinder;
	timer_sleep_ms(15); /* the head settles */
	return true;
}

/* One READ DATA or WRITE DATA of `count` sectors from `sector` (1-based)
 * of cylinder/head, through the buffer: 0, -EROFS or -EIO. */
static int transfer(struct drive *d, bool write, int cylinder, int head, int sector, int count)
{
	const struct format *f = d->format;
	if (!seek(d, cylinder, head)) {
		return -EIO;
	}
	isa_dma_start(DMA_CHANNEL, buffer_phys, (size_t)count * SECTOR, write, false);
	uint8_t cmd[9] = {
		(uint8_t)((write ? CMD_WRITE : CMD_READ) | CMD_MT | CMD_MFM),
		(uint8_t)(head << 2 | d->unit), (uint8_t)cylinder, (uint8_t)head, (uint8_t)sector,
		SIZE_CODE, f->sectors, f->gap, 0xFF,
	};
	uint8_t r[7];
	if (!send_all(cmd, 9) || !wait_irq(2000) || result(r, 7) != 7) {
		isa_dma_stop(DMA_CHANNEL);
		reset();
		return -EIO;
	}
	isa_dma_stop(DMA_CHANNEL);
	if ((r[0] & ST0_CODE) != 0) {
		return (r[1] & ST1_WRITE_PROTECTED) ? -EROFS : -EIO;
	}
	return 0;
}

static int transfer_retrying(struct drive *d, bool write, int cylinder, int head, int sector,
                             int count)
{
	int rc = -EIO;
	for (int attempt = 0; attempt < RETRIES && rc == -EIO; attempt++) {
		if (attempt > 0) {
			d->calibrated = false; /* in case the head is lost */
		}
		rc = transfer(d, write, cylinder, head, sector, count);
	}
	return rc;
}

static void forget(struct drive *d)
{
	for (int i = 0; i < CACHE_SLOTS; i++) {
		if (cache[i].drive == d) {
			cache[i].drive = 0;
		}
	}
}

static struct slot *cached(struct drive *d, int cylinder)
{
	for (int i = 0; i < CACHE_SLOTS; i++) {
		if (cache[i].drive == d && cache[i].cylinder == cylinder) {
			cache[i].used = ++cache_clock;
			return &cache[i];
		}
	}
	return 0;
}

/* Reads a whole cylinder into the cache: its slot, or NULL. */
static struct slot *fill(struct drive *d, int cylinder, uint32_t per_cylinder)
{
	struct slot *victim = 0;
	for (int i = 0; i < CACHE_SLOTS; i++) {
		if (cache[i].data && (!victim || cache[i].used < victim->used)) {
			victim = &cache[i];
		}
	}
	if (!victim || transfer_retrying(d, false, cylinder, 0, 1, (int)per_cylinder) != 0) {
		return 0;
	}
	memcpy(victim->data, buffer, per_cylinder * SECTOR);
	victim->drive = d;
	victim->cylinder = cylinder;
	victim->used = ++cache_clock;
	return victim;
}

/* The disk's format: the first one whose data rate and last sector of
 * cylinder 0 read. */
static const struct format *detect(struct drive *d)
{
	forget(d);
	for (int i = 0; i < 3 && TYPES[d->type].formats[i]; i++) {
		d->format = TYPES[d->type].formats[i];
		outb(FDC_CCR, d->format->rate);
		if (transfer(d, false, 0, d->format->heads - 1, d->format->sectors, 1) == 0) {
			return d->format;
		}
	}
	d->format = 0;
	return 0;
}

/* A new disk (the drive's change line) is found afresh. Reading the
 * line needs the motor on; a seek clears it once a disk is in. */
static void check_change(struct drive *d)
{
	if (!(inb(FDC_DIR) & DIR_CHANGED)) {
		return;
	}
	d->calibrated = false;
	forget(d);
	if (recalibrate(d)) {
		seek(d, 1, 0);
		recalibrate(d);
	}
	const struct format *f = detect(d);
	if (f) {
		d->dev.block_count = (uint32_t)f->cylinders * f->heads * f->sectors;
		kprintf("%s: disk changed: %s\n", d->dev.name, f->name);
	}
}

static int rw(struct device *dev, uint32_t lba, uint32_t count, uint8_t *data, bool write)
{
	struct drive *d = dev->driver_data;
	int rc = 0;
	mutex_lock(&lock);
	select(d);
	check_change(d);
	const struct format *f = d->format;
	if (!f) {
		rc = -EIO;
	}
	while (rc == 0 && count) {
		uint32_t per_cylinder = (uint32_t)f->heads * f->sectors;
		int cylinder = (int)(lba / per_cylinder);
		int head = (int)(lba % per_cylinder / f->sectors);
		int sector = (int)(lba % f->sectors) + 1;
		uint32_t n = per_cylinder - lba % per_cylinder; /* to the cylinder's end */
		n = n < count ? n : count;
		uint32_t offset = lba % per_cylinder * SECTOR;
		struct slot *slot = cached(d, cylinder);
		if (write) {
			memcpy(buffer, data, n * SECTOR);
			rc = transfer_retrying(d, true, cylinder, head, sector, (int)n);
			if (slot && rc == 0) {
				memcpy(slot->data + offset, data, n * SECTOR);
			} else if (slot) {
				slot->drive = 0;
			}
		} else if (slot || (slot = fill(d, cylinder, per_cylinder))) {
			memcpy(data, slot->data + offset, n * SECTOR);
		} else {
			/* A bad sector elsewhere on the cylinder, or no cache: just these. */
			rc = transfer_retrying(d, false, cylinder, head, sector, (int)n);
			if (rc == 0) {
				memcpy(data, buffer, n * SECTOR);
			}
		}
		lba += n;
		count -= n;
		data += n * SECTOR;
	}
	last_used = timer_uptime_ms();
	mutex_unlock(&lock);
	return rc;
}

static int fd_read(struct device *dev, uint32_t lba, uint32_t count, void *buf)
{
	return rw(dev, lba, count, buf, false);
}

static int fd_write(struct device *dev, uint32_t lba, uint32_t count, const void *buf)
{
	return rw(dev, lba, count, (uint8_t *)buf, true);
}

static const struct block_device_ops fd_ops = { .read = fd_read, .write = fd_write };

/* Turns the motors off once the drives have been idle a while. */
static void motor_thread(void *arg)
{
	(void)arg;
	for (;;) {
		timer_sleep_ms(1000);
		mutex_lock(&lock);
		if ((dor & 0xF0) && timer_uptime_ms() - last_used >= MOTOR_IDLE_MS) {
			dor &= 0x0F;
			write_dor();
		}
		mutex_unlock(&lock);
	}
}

void floppy_init(void)
{
	uint8_t types = cmos_read(0x10);
	int type[2] = { types >> 4, types & 0x0F };
	if (!type[0] && !type[1]) {
		return;
	}
	buffer = dma_alloc(BUFFER_BYTES, true, &buffer_phys);
	if (!buffer) {
		kprintf("fd: no memory for DMA\n");
		return;
	}
	/* Cache slots a cylinder of the biggest format the drives take. */
	uint32_t cylinder_bytes = 0;
	for (int u = 0; u < 2; u++) {
		if (type[u] > 0 && type[u] < TYPE_COUNT && TYPES[type[u]].name) {
			const struct format *f = TYPES[type[u]].formats[0];
			uint32_t bytes = (uint32_t)f->heads * f->sectors * SECTOR;
			cylinder_bytes = bytes > cylinder_bytes ? bytes : cylinder_bytes;
		}
	}
	for (int i = 0; i < CACHE_SLOTS && cylinder_bytes; i++) {
		cache[i].data = kmalloc(cylinder_bytes);
	}
	irq_install_handler(IRQ, fdc_irq);
	mutex_lock(&lock);
	uint8_t version = 0, cmd = CMD_VERSION;
	if (!reset() || !send_all(&cmd, 1) || result(&version, 1) != 1) {
		mutex_unlock(&lock);
		kprintf("fd: the floppy controller doesn't answer\n");
		return;
	}
	for (int u = 0; u < 2; u++) {
		struct drive *d = &drives[u];
		if (type[u] <= 0 || type[u] >= TYPE_COUNT || !TYPES[type[u]].name) {
			continue;
		}
		d->unit = u;
		d->type = type[u];
		select(d);
		(void)inb(FDC_DIR);
		const struct format *f = detect(d);
		const struct format *size = f ? f : TYPES[d->type].formats[0];
		ksnprintf(d->dev.name, sizeof(d->dev.name), "fd%d", u);
		d->dev.class = DEVICE_BLOCK;
		d->dev.block_ops = &fd_ops;
		d->dev.block_size = SECTOR;
		d->dev.block_count = (uint32_t)size->cylinders * size->heads * size->sectors;
		d->dev.driver_data = d;
		if (device_register(&d->dev) == 0) {
			kprintf("%s: %s drive, %s%s, on the %s controller\n", d->dev.name,
			        TYPES[d->type].name, f ? f->name : "no disk", f ? " disk" : "",
			        version == 0x90 ? "82077" : "765");
		}
	}
	last_used = timer_uptime_ms();
	mutex_unlock(&lock);
	thread_create("floppy", motor_thread, 0);
}
