#ifndef ZKT_DRIVERS_MBR_H
#define ZKT_DRIVERS_MBR_H

#include "device.h"

/* Reads an MBR partition table from `disk`, if there is one, and
 * registers each primary partition as a block device named
 * "<disk>p1".."<disk>p4". Extended partitions are skipped for now. */
void mbr_scan(struct device *disk);

#endif
