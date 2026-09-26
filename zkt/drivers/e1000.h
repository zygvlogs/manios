#ifndef ZKT_DRIVERS_E1000_H
#define ZKT_DRIVERS_E1000_H

/* Looks for an Intel 8254x PCI card (82540EM and relatives: VirtualBox's
 * PRO/1000 cards, QEMU's -device e1000) and registers it as interface
 * em0. Called by net_init(). */
void e1000_probe(void);

#endif
