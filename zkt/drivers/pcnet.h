#ifndef ZKT_DRIVERS_PCNET_H
#define ZKT_DRIVERS_PCNET_H

/* Looks for an AMD PCnet PCI card (1022:2000: PCnet-PCI II, PCnet-FAST
 * III; VirtualBox's default, QEMU's -device pcnet) and registers it as
 * interface pcn0. Called by net_init(). */
void pcnet_probe(void);

#endif
