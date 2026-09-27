#ifndef ZKT_DRIVERS_VIRTIO_BLK_H
#define ZKT_DRIVERS_VIRTIO_BLK_H

/* Registers each virtio disk on the PCI bus as block device "vd0",
 * "vd1"... (with its MBR partitions). Needs pci_init(). */
void virtio_blk_init(void);

#endif
