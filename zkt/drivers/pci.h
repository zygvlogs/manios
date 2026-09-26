#ifndef ZKT_DRIVERS_PCI_H
#define ZKT_DRIVERS_PCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PCI_DEVICES_MAX 32
#define PCI_BAR_MEM_MASK 0xFFFFFFF0u

struct pci_device {
	uint8_t bus, dev, fn;
	uint16_t vendor, device;
	uint8_t class, subclass, prog_if;
	uint32_t bar[6]; /* as read: low bits flag I/O vs memory */
	uint8_t irq;
};

/* Scans every bus once, at boot. Finds nothing without PCI. */
void pci_init(void);
const struct pci_device *pci_next(const struct pci_device *prev);
const struct pci_device *pci_find(uint16_t vendor, uint16_t device);
uint32_t pci_read32(const struct pci_device *d, uint8_t reg);
void pci_write32(const struct pci_device *d, uint8_t reg, uint32_t value);
/* Lets the device answer its I/O and memory BARs and master the bus
 * (DMA): the command register's bits 0-2. */
void pci_enable(const struct pci_device *d);
/* BAR `i` as an I/O port base or memory address (flag bits cleared), or
 * 0 if it isn't one of that kind. */
uint32_t pci_bar_io(const struct pci_device *d, int i);
uint32_t pci_bar_mem(const struct pci_device *d, int i);

#endif
