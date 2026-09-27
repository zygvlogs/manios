/* Virtio block devices (QEMU's -device virtio-blk-pci, and other
 * hypervisors' paravirtual disks), written from the virtio
 * specification's block device section, over the legacy PCI interface
 * (virtio.h).
 *
 * A request is a chain of three descriptors: a header (read or write,
 * and the sector), the data, and a status byte the device writes. One
 * request at a time, through a bounce buffer below 16 MiB; the driver
 * asks the device not to interrupt and waits for the used ring to move,
 * yielding. Disks are "vd0", "vd1"..., with their MBR partitions. */
#include "virtio_blk.h"
#include <stdbool.h>
#include <stdint.h>
#include "device.h"
#include "dma.h"
#include "heap.h"
#include "io.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "mbr.h"
#include "mutex.h"
#include "sched.h"
#include "timer.h"
#include "virtio.h"

#define DEVICE_LEGACY_BLOCK 0x1001
#define FEATURE_RO (1u << 5)
#define REQ_IN  0
#define REQ_OUT 1
#define REQ_FLUSH 4
#define FEATURE_FLUSH (1u << 9)
#define STATUS_OK 0

#define SECTOR 512
#define BOUNCE_BYTES (64 * 1024)
#define MAX_SECTORS (BOUNCE_BYTES / SECTOR)
#define TIMEOUT_MS 5000
#define MAX_DISKS 4

struct request_header {
	uint32_t type, reserved;
	uint64_t sector;
};

struct vd {
	struct device dev;
	uint16_t io;
	bool read_only, flush;
	struct virtq q;
	struct mutex lock;
	uint8_t *bounce;              /* the data */
	uintptr_t bounce_phys;
	struct request_header *header; /* and, after it, the status byte */
	uintptr_t header_phys;
};

static int disk_count;

/* One request: 0 or -EIO. Caller holds the lock. */
static int request(struct vd *d, uint32_t type, uint64_t sector, uint32_t bytes)
{
	volatile uint8_t *status = (volatile uint8_t *)(d->header + 1);
	d->header->type = type;
	d->header->reserved = 0;
	d->header->sector = sector;
	*status = 0xFF;
	volatile struct vring_desc *desc = d->q.desc;
	desc[0].addr = d->header_phys;
	desc[0].len = sizeof(struct request_header);
	desc[0].flags = VRING_DESC_F_NEXT;
	desc[0].next = bytes ? 1 : 2;
	desc[1].addr = d->bounce_phys;
	desc[1].len = bytes;
	desc[1].flags = (uint16_t)(VRING_DESC_F_NEXT | (type == REQ_IN ? VRING_DESC_F_WRITE : 0));
	desc[1].next = 2;
	desc[2].addr = d->header_phys + sizeof(struct request_header);
	desc[2].len = 1;
	desc[2].flags = VRING_DESC_F_WRITE;
	desc[2].next = 0;
	virtq_submit(d->io, 0, &d->q, 0);

	struct vring_used_elem done;
	uint64_t deadline = timer_uptime_ms() + TIMEOUT_MS;
	while (!virtq_used(&d->q, &done)) {
		if (timer_uptime_ms() > deadline) {
			return -EIO;
		}
		thread_yield();
	}
	return *status == STATUS_OK ? 0 : -EIO;
}

static int vd_read(struct device *dev, uint32_t lba, uint32_t count, void *buf)
{
	struct vd *d = dev->driver_data;
	uint8_t *out = buf;
	int rc = 0;
	mutex_lock(&d->lock);
	while (count && rc == 0) {
		uint32_t n = count < MAX_SECTORS ? count : MAX_SECTORS;
		rc = request(d, REQ_IN, lba, n * SECTOR);
		if (rc == 0) {
			memcpy(out, d->bounce, n * SECTOR);
		}
		lba += n;
		count -= n;
		out += n * SECTOR;
	}
	mutex_unlock(&d->lock);
	return rc;
}

static int vd_write(struct device *dev, uint32_t lba, uint32_t count, const void *buf)
{
	struct vd *d = dev->driver_data;
	const uint8_t *in = buf;
	int rc = 0;
	if (d->read_only) {
		return -EROFS;
	}
	mutex_lock(&d->lock);
	while (count && rc == 0) {
		uint32_t n = count < MAX_SECTORS ? count : MAX_SECTORS;
		memcpy(d->bounce, in, n * SECTOR);
		rc = request(d, REQ_OUT, lba, n * SECTOR);
		lba += n;
		count -= n;
		in += n * SECTOR;
	}
	if (rc == 0 && d->flush) {
		rc = request(d, REQ_FLUSH, 0, 0);
	}
	mutex_unlock(&d->lock);
	return rc;
}

static const struct block_device_ops vd_ops = { .read = vd_read, .write = vd_write };

static void probe(const struct pci_device *pci)
{
	uint16_t io = virtio_start(pci);
	struct vd *d = io ? kmalloc(sizeof(*d)) : 0;
	if (!d) {
		return;
	}
	memset(d, 0, sizeof(*d));
	d->io = io;
	d->lock = (struct mutex)MUTEX_INIT;
	uint32_t features = virtio_features(io, FEATURE_RO | FEATURE_FLUSH);
	d->read_only = features & FEATURE_RO;
	d->flush = features & FEATURE_FLUSH;
	uintptr_t phys;
	uint8_t *page = dma_alloc(64, false, &phys);
	d->bounce = dma_alloc(BOUNCE_BYTES, false, &d->bounce_phys);
	if (!page || !d->bounce || !virtio_queue(io, 0, &d->q) || d->q.size < 3) {
		outb(io + VIRTIO_STATUS, VIRTIO_STATUS_FAILED);
		kprintf("vd: cannot set up the virtio disk at pci %02x:%02x.%x\n", pci->bus, pci->dev,
		        pci->fn);
		return;
	}
	d->header = (struct request_header *)page;
	d->header_phys = phys;
	d->q.avail[0] = VRING_AVAIL_F_NO_INTERRUPT; /* polled */
	virtio_ready(io);

	uint64_t sectors = inl(io + VIRTIO_CONFIG) | (uint64_t)inl(io + VIRTIO_CONFIG + 4) << 32;
	ksnprintf(d->dev.name, sizeof(d->dev.name), "vd%d", disk_count);
	d->dev.class = DEVICE_BLOCK;
	d->dev.block_ops = &vd_ops;
	d->dev.block_size = SECTOR;
	d->dev.block_count = sectors > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)sectors;
	d->dev.driver_data = d;
	if (device_register(&d->dev) != 0) {
		return;
	}
	disk_count++;
	kprintf("%s: virtio disk at pci %02x:%02x.%x, %lu MiB%s\n", d->dev.name, pci->bus, pci->dev,
	        pci->fn, (unsigned long)(sectors / 2048), d->read_only ? ", read-only" : "");
	mbr_scan(&d->dev);
}

void virtio_blk_init(void)
{
	for (const struct pci_device *d = pci_next(0); d && disk_count < MAX_DISKS; d = pci_next(d)) {
		if (d->vendor == VIRTIO_VENDOR && d->device == DEVICE_LEGACY_BLOCK) {
			probe(d);
		}
	}
}
