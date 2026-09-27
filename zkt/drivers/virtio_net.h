#ifndef ZKT_DRIVERS_VIRTIO_NET_H
#define ZKT_DRIVERS_VIRTIO_NET_H

/* Looks for a virtio network card (QEMU's -device virtio-net-pci) and
 * registers it as interface vio0. Called by net_init(). */
void virtio_net_probe(void);

#endif
