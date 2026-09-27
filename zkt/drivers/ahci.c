/* SATA disks on AHCI host controllers (the ICH/PCH SATA controllers and
 * their many compatibles; QEMU's -device ahci), written from Intel's
 * Serial ATA AHCI 1.3 specification and T13's ATA command set.
 *
 * The controller's registers are memory-mapped (BAR 5, "ABAR"): global
 * ones, then 0x80 bytes per port. Each port has a command list (32
 * headers), a place for the FISes the disk sends back, and command
 * tables, all in memory the controller reads by DMA; a command is a
 * host-to-device FIS in a command table with a PRD entry pointing at the
 * data, issued by setting its bit in PxCI and done when the controller
 * clears it. This driver uses one command slot per port and polls
 * (yielding between looks), and moves data through a bounce buffer below
 * 16 MiB. Disks are "sata0", "sata1"..., read with READ DMA EXT and
 * written with WRITE DMA EXT (48-bit LBA), then flushed. CD and DVD
 * drives (ATAPI: the port's signature says) get their SCSI packets in
 * the command table's ATAPI area, sent by the PACKET command, and are
 * atapi.c's "cdN". */
#include "ahci.h"
#include <stdbool.h>
#include <stdint.h>
#include "atapi.h"
#include "device.h"
#include "dma.h"
#include "heap.h"
#include "kerrno.h"
#include "kpage.h"
#include "kprintf.h"
#include "kstring.h"
#include "mbr.h"
#include "mmio.h"
#include "mutex.h"
#include "pci.h"
#include "sched.h"
#include "timer.h"

/* Global registers. */
#define HBA_CAP  0x00
#define HBA_GHC  0x04
#define HBA_PI   0x0C
#define HBA_VS   0x10
#define HBA_CAP2 0x24
#define HBA_BOHC 0x28
#define GHC_AE   (1u << 31) /* AHCI mode */
#define CAP2_BOH (1u << 0)  /* BIOS/OS handoff */
#define BOHC_BOS (1u << 0)
#define BOHC_OOS (1u << 1)

/* Port registers, at 0x100 + 0x80 * port. */
#define PX_CLB  0x00
#define PX_CLBU 0x04
#define PX_FB   0x08
#define PX_FBU  0x0C
#define PX_IS   0x10
#define PX_IE   0x14
#define PX_CMD  0x18
#define PX_TFD  0x20
#define PX_SIG  0x24
#define PX_SSTS 0x28
#define PX_SERR 0x30
#define PX_CI   0x38
#define CMD_ST  (1u << 0)
#define CMD_FRE (1u << 4)
#define CMD_FR  (1u << 14)
#define CMD_CR  (1u << 15)
#define IS_TFES (1u << 30) /* task file error */
#define TFD_ERR 0x01
#define TFD_DRQ 0x08
#define TFD_BSY 0x80
#define SSTS_DET_PRESENT 3
#define SIG_ATA   0x00000101
#define SIG_ATAPI 0xEB140101

#define FIS_H2D 0x27
#define ATA_READ_DMA_EXT  0x25
#define ATA_WRITE_DMA_EXT 0x35
#define ATA_FLUSH_EXT     0xEA
#define ATA_IDENTIFY      0xEC
#define ATA_PACKET        0xA0
#define ATA_IDENTIFY_PACKET 0xA1
#define HEADER_ATAPI (1u << 5)
#define HEADER_WRITE (1u << 6)

#define SECTOR 512
#define BOUNCE_BYTES (64 * 1024)
#define MAX_SECTORS (BOUNCE_BYTES / SECTOR)
#define TIMEOUT_MS 5000
#define MAX_PORTS 32

/* The port's page: its command list (32 headers of 32 bytes), received
 * FIS area (256 bytes, 256-aligned) and one command table (128-aligned:
 * the FIS, the ATAPI command, then one PRD entry). */
#define PAGE_CL 0
#define PAGE_FB 1024
#define PAGE_CT 1280

struct command_header {
	uint16_t flags;      /* FIS length in dwords (bits 0-4), W (bit 6) */
	uint16_t prdt_length;
	volatile uint32_t prdb_count;
	uint32_t table, table_hi;
	uint32_t reserved[4];
};

struct prd {
	uint32_t address, address_hi, reserved;
	uint32_t count; /* bytes - 1; bit 31: interrupt when done */
};

struct command_table {
	uint8_t fis[64];
	uint8_t atapi[16];
	uint8_t reserved[48];
	struct prd prd[1];
};

struct ahci_port {
	volatile uint32_t *port;
	struct command_header *header;
	struct command_table *table;
};

struct ahci_disk {
	struct device dev;
	struct ahci_port p;
	char model[41];
};

struct ahci_cd {
	struct atapi a; /* first: the transport's calls get this */
	struct ahci_port p;
};

static volatile uint32_t *hba;
static struct mutex lock = MUTEX_INIT; /* the bounce buffer, and one command at a time */
static uint8_t *bounce;
static uintptr_t bounce_phys;
static int disk_count;

static uint32_t port_read(volatile uint32_t *port, uint32_t reg)
{
	return port[reg / 4];
}

static void port_write(volatile uint32_t *port, uint32_t reg, uint32_t v)
{
	port[reg / 4] = v;
}

static bool wait_clear(volatile uint32_t *port, uint32_t reg, uint32_t bits, uint32_t ms)
{
	uint64_t deadline = timer_uptime_ms() + ms;
	while (port_read(port, reg) & bits) {
		if (timer_uptime_ms() > deadline) {
			return false;
		}
		thread_yield();
	}
	return true;
}

/* Stops the port's command engine (ST and FRE off, and the controller
 * agreeing: CR and FR clear), as the specification requires before its
 * memory addresses change. */
static bool stop(volatile uint32_t *port)
{
	port_write(port, PX_CMD, port_read(port, PX_CMD) & ~CMD_ST);
	if (!wait_clear(port, PX_CMD, CMD_CR, 500)) {
		return false;
	}
	port_write(port, PX_CMD, port_read(port, PX_CMD) & ~CMD_FRE);
	return wait_clear(port, PX_CMD, CMD_FR, 500);
}

/* Issues the command in slot 0 (with `cdb`, a PACKET command's SCSI
 * packet) and waits for it: 0, or -EIO -- or, for a packet, minus (0x100
 * + the sense key the drive reported). */
static int issue(struct ahci_port *d, uint8_t command, uint64_t lba, uint32_t count,
                 uint32_t bytes, bool write, const uint8_t *cdb)
{
	volatile uint32_t *port = d->port;
	memset(d->table, 0, sizeof(struct command_table));
	uint8_t *fis = d->table->fis;
	fis[0] = FIS_H2D;
	fis[1] = 0x80; /* a command, not a control update */
	fis[2] = command;
	if (cdb) {
		fis[3] = bytes ? 0x01 : 0; /* features: the data by DMA */
		memcpy(d->table->atapi, cdb, 12);
	}
	fis[4] = (uint8_t)lba;
	fis[5] = (uint8_t)(lba >> 8);
	fis[6] = (uint8_t)(lba >> 16);
	fis[7] = 0x40; /* LBA */
	fis[8] = (uint8_t)(lba >> 24);
	fis[9] = (uint8_t)(lba >> 32);
	fis[10] = (uint8_t)(lba >> 40);
	fis[12] = (uint8_t)count;
	fis[13] = (uint8_t)(count >> 8);
	d->header->flags = (uint16_t)(5 | (write ? HEADER_WRITE : 0) | (cdb ? HEADER_ATAPI : 0)); /* 5 dwords */
	d->header->prdt_length = bytes ? 1 : 0;
	d->header->prdb_count = 0;
	if (bytes) {
		d->table->prd[0].address = (uint32_t)bounce_phys;
		d->table->prd[0].address_hi = 0;
		d->table->prd[0].count = bytes - 1;
	}
	if (!wait_clear(port, PX_TFD, TFD_BSY | TFD_DRQ, TIMEOUT_MS)) {
		return -EIO;
	}
	port_write(port, PX_IS, 0xFFFFFFFFu);
	port_write(port, PX_CI, 1);
	uint64_t deadline = timer_uptime_ms() + TIMEOUT_MS;
	while (port_read(port, PX_CI) & 1) {
		if ((port_read(port, PX_IS) & IS_TFES) || timer_uptime_ms() > deadline) {
			/* The error register is in PxTFD's bits 15-8; a packet
			 * device's sense key in its high four. */
			uint32_t tfd = port_read(port, PX_TFD);
			bool error = port_read(port, PX_IS) & IS_TFES;
			/* Restart the engine to clear the error (AHCI 6.2.2.1). */
			stop(port);
			port_write(port, PX_SERR, 0xFFFFFFFFu);
			port_write(port, PX_IS, 0xFFFFFFFFu);
			port_write(port, PX_CMD, port_read(port, PX_CMD) | CMD_FRE | CMD_ST);
			return cdb && error ? -(int)(0x100 + ((tfd >> 12) & 0x0F)) : -EIO;
		}
		thread_yield();
	}
	return (port_read(port, PX_TFD) & TFD_ERR) ? -EIO : 0;
}

/* The ATAPI transport over this port (atapi.h). */
static long packet(struct atapi *a, const uint8_t cdb[12], void *buf, uint32_t len)
{
	struct ahci_cd *cd = (struct ahci_cd *)a;
	uint32_t bytes = (len + 1) & ~1u; /* PRD lengths are even */
	if (bytes > BOUNCE_BYTES) {
		return -EIO;
	}
	int rc = issue(&cd->p, ATA_PACKET, (uint64_t)(bytes & 0xFFFF) << 8, 0, bytes, false, cdb);
	if (rc) {
		return rc;
	}
	uint32_t got = cd->p.header->prdb_count;
	memcpy(buf, bounce, got < len ? got : len);
	return (long)got;
}

static int sata_read(struct device *dev, uint32_t lba, uint32_t count, void *buf)
{
	struct ahci_disk *d = dev->driver_data;
	uint8_t *out = buf;
	int rc = 0;
	mutex_lock(&lock);
	while (count && rc == 0) {
		uint32_t n = count < MAX_SECTORS ? count : MAX_SECTORS;
		rc = issue(&d->p, ATA_READ_DMA_EXT, lba, n, n * SECTOR, false, 0);
		if (rc == 0) {
			memcpy(out, bounce, n * SECTOR);
		}
		lba += n;
		count -= n;
		out += n * SECTOR;
	}
	mutex_unlock(&lock);
	return rc;
}

static int sata_write(struct device *dev, uint32_t lba, uint32_t count, const void *buf)
{
	struct ahci_disk *d = dev->driver_data;
	const uint8_t *in = buf;
	int rc = 0;
	mutex_lock(&lock);
	while (count && rc == 0) {
		uint32_t n = count < MAX_SECTORS ? count : MAX_SECTORS;
		memcpy(bounce, in, n * SECTOR);
		rc = issue(&d->p, ATA_WRITE_DMA_EXT, lba, n, n * SECTOR, true, 0);
		lba += n;
		count -= n;
		in += n * SECTOR;
	}
	if (rc == 0) {
		rc = issue(&d->p, ATA_FLUSH_EXT, 0, 0, 0, false, 0);
	}
	mutex_unlock(&lock);
	return rc;
}

static const struct block_device_ops sata_ops = { .read = sata_read, .write = sata_write };

static void model_string(char *dst, const uint16_t *id)
{
	for (int i = 0; i < 20; i++) {
		dst[2 * i] = (char)(id[27 + i] >> 8);
		dst[2 * i + 1] = (char)(id[27 + i] & 0xFF);
	}
	int len = 40;
	while (len > 0 && dst[len - 1] == ' ') {
		len--;
	}
	dst[len] = '\0';
}

/* Points the port at its memory and starts its command engine. */
static bool setup_port(volatile uint32_t *port, struct ahci_port *ap)
{
	uintptr_t phys;
	uint8_t *page = kpage_alloc(&phys);
	if (!page || !stop(port)) {
		return false;
	}
	ap->port = port;
	ap->header = (struct command_header *)(page + PAGE_CL);
	ap->table = (struct command_table *)(page + PAGE_CT);
	ap->header->table = (uint32_t)phys + PAGE_CT;
	port_write(port, PX_CLB, (uint32_t)phys + PAGE_CL);
	port_write(port, PX_CLBU, 0);
	port_write(port, PX_FB, (uint32_t)phys + PAGE_FB);
	port_write(port, PX_FBU, 0);
	port_write(port, PX_SERR, 0xFFFFFFFFu);
	port_write(port, PX_IS, 0xFFFFFFFFu);
	port_write(port, PX_IE, 0); /* polled */
	port_write(port, PX_CMD, port_read(port, PX_CMD) | CMD_FRE);
	port_write(port, PX_CMD, port_read(port, PX_CMD) | CMD_ST);
	return true;
}

/* IDENTIFY (PACKET) DEVICE's 256 words. */
static bool identify(struct ahci_port *ap, uint8_t command, uint16_t *id)
{
	mutex_lock(&lock);
	int rc = issue(ap, command, 0, 0, SECTOR, false, 0);
	memcpy(id, bounce, SECTOR);
	mutex_unlock(&lock);
	return rc == 0;
}

static void probe_cd(int p, volatile uint32_t *port)
{
	struct ahci_cd *cd = kmalloc(sizeof(*cd));
	uint16_t id[256];
	if (!cd) {
		return;
	}
	memset(cd, 0, sizeof(*cd));
	if (!setup_port(port, &cd->p) || !identify(&cd->p, ATA_IDENTIFY_PACKET, id)
	    || ((id[0] >> 8) & 0x1F) != 5) {
		kprintf("ahci: port %d: a packet device, not a CD drive, left alone\n", p);
		return;
	}
	model_string(cd->a.model, id);
	cd->a.packet = packet;
	cd->a.lock = &lock;
	atapi_register(&cd->a, "SATA");
}

static void probe_port(int p)
{
	volatile uint32_t *port = hba + (0x100 + 0x80 * p) / 4;
	if ((port_read(port, PX_SSTS) & 0x0F) != SSTS_DET_PRESENT) {
		return;
	}
	uint32_t sig = port_read(port, PX_SIG);
	if (sig == SIG_ATAPI) {
		probe_cd(p, port);
		return;
	}
	if (sig != SIG_ATA) {
		kprintf("ahci: port %d: a device of signature %08lx, left alone\n", p,
		        (unsigned long)sig);
		return;
	}
	struct ahci_disk *d = kmalloc(sizeof(*d));
	uint16_t id[256];
	if (!d) {
		return;
	}
	memset(d, 0, sizeof(*d));
	if (!setup_port(port, &d->p) || !identify(&d->p, ATA_IDENTIFY, id)) {
		kprintf("ahci: port %d: cannot set the disk up\n", p);
		return;
	}
	/* Words 100-103: the 48-bit sector count; 60-61 the 28-bit one. */
	uint64_t sectors = (uint64_t)id[100] | (uint64_t)id[101] << 16 | (uint64_t)id[102] << 32
	                   | (uint64_t)id[103] << 48;
	if (!(id[83] & (1u << 10))) {
		kprintf("ahci: port %d: the disk has no 48-bit LBA, left alone\n", p);
		return;
	}
	if (!sectors) {
		sectors = (uint32_t)id[60] | (uint32_t)id[61] << 16;
	}
	model_string(d->model, id);
	ksnprintf(d->dev.name, sizeof(d->dev.name), "sata%d", disk_count);
	d->dev.class = DEVICE_BLOCK;
	d->dev.block_ops = &sata_ops;
	d->dev.block_size = SECTOR;
	d->dev.block_count = sectors > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)sectors;
	d->dev.driver_data = d;
	if (device_register(&d->dev) != 0) {
		return;
	}
	disk_count++;
	kprintf("%s: %s, %lu MiB, on AHCI port %d\n", d->dev.name, d->model,
	        (unsigned long)(sectors / 2048), p);
	mbr_scan(&d->dev);
}

static void init_controller(const struct pci_device *pci)
{
	uint32_t abar = pci_bar_mem(pci, 5);
	if (!abar || hba) {
		return; /* one controller for now */
	}
	pci_enable(pci);
	hba = mmio_map(abar, 0x1100);
	if (!hba) {
		kprintf("ahci: cannot map its registers\n");
		return;
	}
	/* Take the controller from the BIOS, if it owns it (AHCI 10.6). */
	if (hba[HBA_CAP2 / 4] & CAP2_BOH) {
		hba[HBA_BOHC / 4] |= BOHC_OOS;
		uint64_t deadline = timer_uptime_ms() + 2000;
		while ((hba[HBA_BOHC / 4] & BOHC_BOS) && timer_uptime_ms() < deadline) {
			thread_yield();
		}
	}
	hba[HBA_GHC / 4] |= GHC_AE;
	bounce = dma_alloc(BOUNCE_BYTES, false, &bounce_phys);
	if (!bounce) {
		kprintf("ahci: no memory for DMA\n");
		return;
	}
	uint32_t vs = hba[HBA_VS / 4], ports = hba[HBA_PI / 4];
	kprintf("ahci: AHCI %lu.%lu controller (%04x:%04x) at pci %02x:%02x.%x, %lu slots\n",
	        (unsigned long)(vs >> 16), (unsigned long)((vs >> 8) & 0xFF), pci->vendor,
	        pci->device, pci->bus, pci->dev, pci->fn,
	        (unsigned long)(((hba[HBA_CAP / 4] >> 8) & 0x1F) + 1));
	for (int p = 0; p < MAX_PORTS; p++) {
		if (ports & (1u << p)) {
			probe_port(p);
		}
	}
}

void ahci_init(void)
{
	for (const struct pci_device *d = pci_next(0); d; d = pci_next(d)) {
		/* Mass storage, SATA, AHCI 1.0 programming interface. */
		if (d->class == 0x01 && d->subclass == 0x06 && d->prog_if == 0x01) {
			init_controller(d);
		}
	}
}
