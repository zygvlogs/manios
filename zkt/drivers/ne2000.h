#ifndef ZKT_DRIVERS_NE2000_H
#define ZKT_DRIVERS_NE2000_H

/* Looks for an NE2000-compatible card at I/O 0x300, IRQ 9 (QEMU:
 * -device ne2k_isa,netdev=...), and registers it as interface ne0.
 * Called by net_init(). */
void ne2000_probe(void);

#endif
