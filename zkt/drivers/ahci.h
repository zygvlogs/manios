#ifndef ZKT_DRIVERS_AHCI_H
#define ZKT_DRIVERS_AHCI_H

/* Finds an AHCI SATA controller on the PCI bus and registers its disks
 * as block devices "sata0", "sata1"... (with their MBR partitions).
 * Needs pci_init(). */
void ahci_init(void);

#endif
