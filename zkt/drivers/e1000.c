/* Intel 8254x gigabit Ethernet ("e1000": 82540EM and relatives), the
 * PRO/1000 cards VirtualBox offers and QEMU's -device e1000. Written from
 * Intel's PCI/PCI-X Family of Gigabit Ethernet Controllers Software
 * Developer's Manual (8254x): registers in memory (BAR 0), legacy
 * receive and transmit descriptors in rings the card reads by DMA.
 *
 * Receive: RX_COUNT buffers of 2048 bytes; the card fills a descriptor
 * and sets DD, poll() hands the frame on and gives the descriptor back
 * by moving the tail (RDT). Transmit: the frame is copied to the slot at
 * the tail, which moves on; a slot is reused once its DD is back. */
#include "e1000.h"
#include <stdbool.h>
#include "irq.h"
#include "kerrno.h"
#include "kpage.h"
#include "kprintf.h"
#include "kstring.h"
#include "mmio.h"
#include "mutex.h"
#include "net.h"
#include "pci.h"
#include "timer.h"

/* 8254x parts with this programming model (and the legacy descriptors):
 * 82540EM is VirtualBox's "PRO/1000 MT Desktop" and QEMU's e1000;
 * 82543GC and 82545EM its "T Server" and "MT Server". */
static const uint16_t DEVICES[] = {
	0x100E, 0x100F, 0x1004, 0x1015, 0x1016, 0x1017, 0x101E, 0x1026, 0x1076, 0x1079,
};

#define REG_CTRL   0x0000
#define REG_STATUS 0x0008
#define REG_EERD   0x0014
#define REG_ICR    0x00C0
#define REG_IMS    0x00D0
#define REG_IMC    0x00D8
#define REG_RCTL   0x0100
#define REG_TCTL   0x0400
#define REG_TIPG   0x0410
#define REG_RDBAL  0x2800
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818
#define REG_RDTR   0x2820
#define REG_TDBAL  0x3800
#define REG_TDBAH  0x3804
#define REG_TDLEN  0x3808
#define REG_TDH    0x3810
#define REG_TDT    0x3818
#define REG_MTA    0x5200
#define REG_RAL0   0x5400
#define REG_RAH0   0x5404

#define CTRL_LRST    (1u << 3)
#define CTRL_ASDE    (1u << 5)
#define CTRL_SLU     (1u << 6)
#define CTRL_ILOS    (1u << 7)
#define CTRL_RST     (1u << 26)
#define CTRL_VME     (1u << 30)
#define CTRL_PHY_RST (1u << 31)
#define STATUS_LU    (1u << 1)
#define EERD_START   (1u << 0)
#define EERD_DONE    (1u << 4)
#define RAH_AV       (1u << 31)
#define RCTL_EN      (1u << 1)
#define RCTL_BAM     (1u << 15)  /* broadcasts too */
#define RCTL_SECRC   (1u << 26)  /* strip the FCS; BSIZE 00: 2048-byte buffers */
#define TCTL_EN      (1u << 1)
#define TCTL_PSP     (1u << 3)   /* pad short frames */
#define TCTL_CT      (0x0Fu << 4)
#define TCTL_COLD    (0x40u << 12)
#define ICR_LSC      (1u << 2)
#define ICR_RXDMT0   (1u << 4)
#define ICR_RXO      (1u << 6)
#define ICR_RXT0     (1u << 7)
#define ICR_RX       (ICR_RXDMT0 | ICR_RXO | ICR_RXT0)

#define RX_COUNT 32 /* ring lengths are multiples of 128 bytes: 8 descriptors */
#define TX_COUNT 8
#define BUF_SIZE 2048

struct rx_desc {
	uint32_t addr_lo, addr_hi;
	uint16_t length, checksum;
	uint8_t status, errors;
	uint16_t special;
};

struct tx_desc {
	uint32_t addr_lo, addr_hi;
	uint16_t length;
	uint8_t cso, cmd, status, css;
	uint16_t special;
};

#define RX_DD  0x01
#define RX_EOP 0x02
#define TX_EOP 0x01
#define TX_IFCS 0x02
#define TX_RS  0x08
#define TX_DD  0x01
#define TX_EC  0x02 /* excess collisions */
#define TX_LC  0x04 /* late collision */

static struct {
	volatile uint32_t *regs;
	struct netif ifc;
	volatile struct rx_desc *rx;
	volatile struct tx_desc *tx;
	uint8_t *rx_buf[RX_COUNT], *tx_buf[TX_COUNT];
	unsigned rx_next, tx_next;
	struct mutex tx_lock;
} card = { .tx_lock = MUTEX_INIT };

#define barrier() __asm__ volatile("" ::: "memory")

static uint32_t reg_read(uint32_t reg)
{
	return card.regs[reg / 4];
}

static void reg_write(uint32_t reg, uint32_t v)
{
	card.regs[reg / 4] = v;
}

/* One word of the EEPROM (for parts that didn't load the address). */
static bool eeprom_read(uint8_t word, uint16_t *out)
{
	reg_write(REG_EERD, (uint32_t)word << 8 | EERD_START);
	for (int i = 0; i < 1000; i++) {
		uint32_t v = reg_read(REG_EERD);
		if (v & EERD_DONE) {
			*out = (uint16_t)(v >> 16);
			return true;
		}
	}
	return false;
}

static bool read_mac(uint8_t mac[ETH_ALEN])
{
	uint32_t lo = reg_read(REG_RAL0), hi = reg_read(REG_RAH0);
	if ((hi & RAH_AV) && (lo || (hi & 0xFFFF))) {
		for (int i = 0; i < 4; i++) {
			mac[i] = (uint8_t)(lo >> (8 * i));
		}
		mac[4] = (uint8_t)hi;
		mac[5] = (uint8_t)(hi >> 8);
		return true;
	}
	for (uint8_t w = 0; w < 3; w++) {
		uint16_t v;
		if (!eeprom_read(w, &v)) {
			return false;
		}
		mac[2 * w] = (uint8_t)v;
		mac[2 * w + 1] = (uint8_t)(v >> 8);
	}
	return true;
}

static bool allocate(uint32_t *rx_phys, uint32_t *tx_phys)
{
	uintptr_t phys, buf_phys = 0;
	uint8_t *page = kpage_alloc(&phys), *buf_page = 0;
	if (!page) {
		return false;
	}
	card.rx = (volatile struct rx_desc *)page; /* 512 bytes */
	card.tx = (volatile struct tx_desc *)(page + 1024);
	*rx_phys = (uint32_t)phys;
	*tx_phys = (uint32_t)phys + 1024;
	for (unsigned i = 0; i < RX_COUNT + TX_COUNT; i++) {
		if (i % 2 == 0 && !(buf_page = kpage_alloc(&buf_phys))) {
			return false;
		}
		uint8_t *buf = buf_page + (i % 2) * BUF_SIZE;
		uint32_t addr = (uint32_t)buf_phys + (i % 2) * BUF_SIZE;
		if (i < RX_COUNT) {
			card.rx_buf[i] = buf;
			card.rx[i].addr_lo = addr;
		} else {
			card.tx_buf[i - RX_COUNT] = buf;
			card.tx[i - RX_COUNT].addr_lo = addr;
			card.tx[i - RX_COUNT].status = TX_DD; /* free */
		}
	}
	return true;
}

static void e1000_irq(void)
{
	uint32_t icr = reg_read(REG_ICR); /* reading clears it */
	if (icr & ICR_RXO) {
		card.ifc.rx_dropped++;
	}
	if (icr & ICR_RX) {
		netif_notify();
	}
}

static void e1000_poll(struct netif *ifc)
{
	for (;;) {
		volatile struct rx_desc *d = &card.rx[card.rx_next];
		if (!(d->status & RX_DD)) {
			break;
		}
		barrier();
		if ((d->status & RX_EOP) && !d->errors && d->length <= ETH_FRAME_MAX) {
			netif_input(ifc, card.rx_buf[card.rx_next], d->length);
		} else {
			ifc->rx_dropped++;
		}
		d->status = 0;
		barrier();
		reg_write(REG_RDT, card.rx_next); /* the card may fill it again */
		card.rx_next = (card.rx_next + 1) % RX_COUNT;
	}
}

static int e1000_transmit(struct netif *ifc, const uint8_t *frame, size_t len)
{
	if (len > ETH_FRAME_MAX) {
		return -EINVAL;
	}
	mutex_lock(&card.tx_lock);
	volatile struct tx_desc *d = &card.tx[card.tx_next];
	for (int waited = 0; !(d->status & TX_DD); waited++) {
		if (waited == 100) {
			mutex_unlock(&card.tx_lock);
			ifc->tx_errors++;
			return -ETIMEDOUT;
		}
		timer_sleep_ms(1);
	}
	if (d->status & (TX_EC | TX_LC)) {
		ifc->tx_errors++; /* that earlier frame */
	}
	uint8_t *buf = card.tx_buf[card.tx_next];
	memcpy(buf, frame, len);
	if (len < ETH_FRAME_MIN) {
		memset(buf + len, 0, ETH_FRAME_MIN - len);
		len = ETH_FRAME_MIN;
	}
	d->length = (uint16_t)len;
	d->cso = 0;
	d->css = 0;
	d->special = 0;
	d->status = 0;
	d->cmd = TX_EOP | TX_IFCS | TX_RS;
	barrier();
	card.tx_next = (card.tx_next + 1) % TX_COUNT;
	reg_write(REG_TDT, card.tx_next);
	mutex_unlock(&card.tx_lock);
	ifc->tx_frames++;
	return 0;
}

static const struct pci_device *find(void)
{
	for (const struct pci_device *d = pci_next(0); d; d = pci_next(d)) {
		for (size_t i = 0; d->vendor == 0x8086 && i < sizeof(DEVICES) / sizeof(DEVICES[0]); i++) {
			if (d->device == DEVICES[i]) {
				return d;
			}
		}
	}
	return 0;
}

void e1000_probe(void)
{
	const struct pci_device *pci = find();
	uint32_t mmio = pci ? pci_bar_mem(pci, 0) : 0;
	if (!mmio) {
		return;
	}
	if (pci->irq == 0 || pci->irq >= 16) {
		kprintf("em0: 8254x at pci %02x:%02x.%x has no interrupt line\n", pci->bus, pci->dev,
		        pci->fn);
		return;
	}
	pci_enable(pci);
	card.regs = mmio_map(mmio, 128 * 1024);
	if (!card.regs) {
		kprintf("em0: cannot map its registers\n");
		return;
	}
	reg_write(REG_IMC, 0xFFFFFFFFu);
	reg_write(REG_CTRL, reg_read(REG_CTRL) | CTRL_RST);
	timer_sleep_ms(2);
	reg_write(REG_IMC, 0xFFFFFFFFu);
	(void)reg_read(REG_ICR);
	reg_write(REG_CTRL, (reg_read(REG_CTRL) | CTRL_SLU | CTRL_ASDE)
	                        & ~(CTRL_LRST | CTRL_ILOS | CTRL_VME | CTRL_PHY_RST));

	uint8_t *mac = card.ifc.mac;
	uint32_t rx_phys, tx_phys;
	if (!read_mac(mac)) {
		kprintf("em0: cannot read its Ethernet address\n");
		return;
	}
	if (!allocate(&rx_phys, &tx_phys)) {
		kprintf("em0: out of memory\n");
		return;
	}
	reg_write(REG_RAL0, (uint32_t)mac[0] | (uint32_t)mac[1] << 8 | (uint32_t)mac[2] << 16
	                        | (uint32_t)mac[3] << 24);
	reg_write(REG_RAH0, (uint32_t)mac[4] | (uint32_t)mac[5] << 8 | RAH_AV);
	for (int i = 0; i < 128; i++) {
		reg_write(REG_MTA + 4 * i, 0); /* no multicast */
	}

	reg_write(REG_RDBAL, rx_phys);
	reg_write(REG_RDBAH, 0);
	reg_write(REG_RDLEN, RX_COUNT * sizeof(struct rx_desc));
	reg_write(REG_RDH, 0);
	reg_write(REG_RDT, RX_COUNT - 1);
	reg_write(REG_RDTR, 0);
	reg_write(REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);

	reg_write(REG_TDBAL, tx_phys);
	reg_write(REG_TDBAH, 0);
	reg_write(REG_TDLEN, TX_COUNT * sizeof(struct tx_desc));
	reg_write(REG_TDH, 0);
	reg_write(REG_TDT, 0);
	reg_write(REG_TIPG, 10 | 8 << 10 | 6 << 20);
	reg_write(REG_TCTL, TCTL_EN | TCTL_PSP | TCTL_CT | TCTL_COLD);

	strlcpy(card.ifc.name, "em0", sizeof(card.ifc.name));
	card.ifc.transmit = e1000_transmit;
	card.ifc.poll = e1000_poll;
	card.ifc.driver = &card;
	irq_install_handler(pci->irq, e1000_irq);
	reg_write(REG_IMS, ICR_RX | ICR_LSC);
	(void)reg_read(REG_ICR);
	netif_register(&card.ifc);
	kprintf("em0: Intel 8254x (%04x) at pci %02x:%02x.%x, irq %d, %02x:%02x:%02x:%02x:%02x:%02x, "
	        "link %s\n", pci->device, pci->bus, pci->dev, pci->fn, pci->irq, mac[0], mac[1], mac[2],
	        mac[3], mac[4], mac[5], (reg_read(REG_STATUS) & STATUS_LU) ? "up" : "down");
}
