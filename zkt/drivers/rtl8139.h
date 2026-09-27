#ifndef ZKT_DRIVERS_RTL8139_H
#define ZKT_DRIVERS_RTL8139_H

/* Looks for a RealTek RTL8139 PCI card (QEMU's -device rtl8139) and
 * registers it as interface rl0. Called by net_init(). */
void rtl8139_probe(void);

#endif
