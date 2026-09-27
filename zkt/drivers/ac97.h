#ifndef ZKT_DRIVERS_AC97_H
#define ZKT_DRIVERS_AC97_H

/* Looks for an Intel ICH AC'97 controller on the PCI bus and, found,
 * plays /dev/audio through it (unless a Sound Blaster has it). Needs
 * pci_init(). */
void ac97_init(void);

#endif
