#ifndef ZKT_DRIVERS_ATA_H
#define ZKT_DRIVERS_ATA_H

/* Probes both legacy IDE channels (0x1F0 and 0x170, master and slave)
 * and registers each ATA disk as block device "ata0".."ata3" (channel *
 * 2 + slave), then its MBR partitions as "ata0p1" etc. Read-only for
 * now. Needs the scheduler and the timer (for timeouts). */
void ata_init(void);

#endif
