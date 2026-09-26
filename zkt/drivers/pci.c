/* PCI configuration space through configuration mechanism #1 (I/O ports
 * 0xCF8/0xCFC), from the PCI Local Bus Specification. A 386 or 486 with
 * only ISA slots has no PCI; the probe then finds nothing. */
#include "pci.h"
#include "io.h"
#include "kprintf.h"

#define CONFIG_ADDRESS 0xCF8
#define CONFIG_DATA    0xCFC
#define ENABLE         0x80000000u

static struct pci_device devices[PCI_DEVICES_MAX];
static size_t device_count;

static uint32_t address(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg)
{
	return ENABLE | (uint32_t)bus << 16 | (uint32_t)dev << 11 | (uint32_t)fn << 8 | (reg & 0xFC);
}

uint32_t pci_read32(const struct pci_device *d, uint8_t reg)
{
	outl(CONFIG_ADDRESS, address(d->bus, d->dev, d->fn, reg));
	return inl(CONFIG_DATA);
}

void pci_write32(const struct pci_device *d, uint8_t reg, uint32_t value)
{
	outl(CONFIG_ADDRESS, address(d->bus, d->dev, d->fn, reg));
	outl(CONFIG_DATA, value);
}

static uint32_t read_raw(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg)
{
	outl(CONFIG_ADDRESS, address(bus, dev, fn, reg));
	return inl(CONFIG_DATA);
}

/* Mechanism #1 exists if the address register keeps what is written. */
static bool present(void)
{
	uint32_t saved = inl(CONFIG_ADDRESS);
	outl(CONFIG_ADDRESS, ENABLE);
	bool ok = inl(CONFIG_ADDRESS) == ENABLE;
	outl(CONFIG_ADDRESS, saved);
	return ok;
}

static void add(uint8_t bus, uint8_t dev, uint8_t fn, uint32_t id)
{
	if (device_count == PCI_DEVICES_MAX) {
		return;
	}
	struct pci_device *d = &devices[device_count++];
	d->bus = bus;
	d->dev = dev;
	d->fn = fn;
	d->vendor = (uint16_t)id;
	d->device = (uint16_t)(id >> 16);
	uint32_t class = read_raw(bus, dev, fn, 0x08);
	d->class = (uint8_t)(class >> 24);
	d->subclass = (uint8_t)(class >> 16);
	d->prog_if = (uint8_t)(class >> 8);
	for (int i = 0; i < 6; i++) {
		d->bar[i] = read_raw(bus, dev, fn, (uint8_t)(0x10 + 4 * i));
	}
	d->irq = (uint8_t)read_raw(bus, dev, fn, 0x3C);
}

void pci_init(void)
{
	if (!present()) {
		return;
	}
	for (unsigned bus = 0; bus < 256; bus++) {
		for (unsigned dev = 0; dev < 32; dev++) {
			uint32_t id = read_raw((uint8_t)bus, (uint8_t)dev, 0, 0);
			if ((id & 0xFFFF) == 0xFFFF) {
				continue;
			}
			add((uint8_t)bus, (uint8_t)dev, 0, id);
			bool multifunction = read_raw((uint8_t)bus, (uint8_t)dev, 0, 0x0C) & 0x00800000;
			for (unsigned fn = 1; multifunction && fn < 8; fn++) {
				id = read_raw((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0);
				if ((id & 0xFFFF) != 0xFFFF) {
					add((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, id);
				}
			}
		}
	}
	kprintf("pci: %lu devices\n", (unsigned long)device_count);
}

const struct pci_device *pci_next(const struct pci_device *prev)
{
	size_t i = prev ? (size_t)(prev - devices) + 1 : 0;
	return i < device_count ? &devices[i] : 0;
}

const struct pci_device *pci_find(uint16_t vendor, uint16_t device)
{
	for (size_t i = 0; i < device_count; i++) {
		if (devices[i].vendor == vendor && devices[i].device == device) {
			return &devices[i];
		}
	}
	return 0;
}

void pci_enable(const struct pci_device *d)
{
	uint32_t command = pci_read32(d, 0x04) & 0xFFFF; /* not the status bits */
	pci_write32(d, 0x04, command | 0x7);
}

uint32_t pci_bar_io(const struct pci_device *d, int i)
{
	return (d->bar[i] & 1) ? d->bar[i] & 0xFFFFFFFCu : 0;
}

uint32_t pci_bar_mem(const struct pci_device *d, int i)
{
	return (d->bar[i] & 1) ? 0 : d->bar[i] & PCI_BAR_MEM_MASK;
}
