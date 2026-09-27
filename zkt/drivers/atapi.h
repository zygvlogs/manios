/* CD and DVD drives (ATAPI: SCSI's MMC commands in 12-byte packets),
 * whatever carries the packets -- the IDE ports (ata.c) or an AHCI
 * port (ahci.c). The transport fills in `packet`, `lock` and `model`
 * and registers the drive; atapi.c makes it block device "cdN", 2048-byte
 * blocks, read only, and notices discs coming, going and changing. */
#ifndef ZKT_DRIVERS_ATAPI_H
#define ZKT_DRIVERS_ATAPI_H

#include <stdbool.h>
#include <stdint.h>
#include "device.h"
#include "mutex.h"

#define CD_BLOCK 2048
#define CD_MAX_BLOCKS_PER_COMMAND 32 /* 64 KiB */

struct atapi {
	struct device dev;
	/* Sends a packet and moves up to `len` bytes of its data in: the
	 * bytes moved, -EIO, or minus (0x100 + the sense key) when the
	 * drive reported an error. Called with *lock held. */
	long (*packet)(struct atapi *a, const uint8_t cdb[12], void *buf, uint32_t len);
	struct mutex *lock;
	char model[41];
	bool measured; /* block_count is the disc's */
};

/* Names it "cd0", "cd1"..., measures the disc (if any), registers it and
 * says so, `how` naming the transport ("ATAPI", "SATA"). 0 or an error. */
int atapi_register(struct atapi *a, const char *how);

#endif
