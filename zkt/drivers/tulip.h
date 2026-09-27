#ifndef ZKT_DRIVERS_TULIP_H
#define ZKT_DRIVERS_TULIP_H

/* Looks for a DEC 21143 "Tulip" PCI card (or a 21140 or 21041; QEMU's
 * -device tulip) and registers it as interface dc0. Called by
 * net_init(). */
void tulip_probe(void);

#endif
