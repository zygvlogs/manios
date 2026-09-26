/* AMD PCnet PCI Ethernet (Am79C970A PCnet-PCI II, Am79C973 PCnet-FAST
 * III): VirtualBox's default network card, and QEMU's -device pcnet.
 * Written from the Am79C970A datasheet's register and descriptor
 * descriptions.
 *
 * The chip masters the bus: it reads an initialization block, then
 * receive and transmit descriptor rings, from memory, and moves frames
 * to and from buffers by DMA. Software style 2 (BCR20): 32-bit
 * structures. Registers are reached through an address port (RAP) and
 * a data port (RDP for the CSRs, BDP for the BCRs), 16 or 32 bits wide
 * depending on how the chip was last used; both are handled. The IRQ
 * handler acknowledges and wakes the network thread, which takes frames
 * off the receive ring in poll(). */
#include "pcnet.h"
#include <stdbool.h>
#include "cpu.h"
#include "io.h"
#include "irq.h"
#include "kerrno.h"
#include "kpage.h"
#include "kprintf.h"
#include "kstring.h"
#include "mutex.h"
#include "net.h"
#include "pci.h"
#include "timer.h"

#define PCNET_VENDOR 0x1022
#define PCNET_DEVICE 0x2000

/* I/O offsets: the address PROM, then the ports in word (WIO) or
 * double-word (DWIO) mode. */
#define APROM     0x00
#define WIO_RDP   0x10
#define WIO_RAP   0x12
#define WIO_RESET 0x14
#define WIO_BDP   0x16
#define DWIO_RDP   0x10
#define DWIO_RAP   0x14
#define DWIO_RESET 0x18
#define DWIO_BDP   0x1C

/* CSR0, the controller status register. Bits 8-14 are cleared by
 * writing 1; the low bits are commands. */
#define CSR0_INIT 0x0001
#define CSR0_STRT 0x0002
#define CSR0_STOP 0x0004
#define CSR0_TDMD 0x0008
#define CSR0_IENA 0x0040
#define CSR0_INTR 0x0080
#define CSR0_IDON 0x0100
#define CSR0_RINT 0x0400
#define CSR0_MISS 0x1000
#define CSR0_ACK  0x7F00 /* BABL CERR MISS MERR RINT TINT IDON */
#define CSR3_IDONM 0x0100
#define CSR3_TINTM 0x0200
#define CSR4_APAD_XMT 0x0800
#define BCR20_SWSTYLE2 0x0002

/* Descriptor bits (RMD1/TMD1): the card owns it; start and end of a
 * frame; an error. The low 12 bits are the buffer size, negated, and
 * bits 12-15 must be ones. */
#define DESC_OWN 0x80000000u
#define DESC_ERR 0x40000000u
#define DESC_STP 0x02000000u
#define DESC_ENP 0x01000000u
#define DESC_ONES 0x0000F000u

#define RX_COUNT 32 /* RLEN = 5 */
#define TX_COUNT 8  /* TLEN = 3 */
#define BUF_SIZE 1536
#define BUFS_PER_PAGE 2

struct init_block {
	uint32_t mode_lengths; /* MODE, RLEN in bits 20-23, TLEN in 28-31 */
	uint8_t padr[6];
	uint16_t reserved;
	uint32_t ladrf[2];
	uint32_t rdra, tdra;
};

struct desc {
	uint32_t addr, flags, misc, user;
};

static struct {
	uint16_t io;
	bool dwio;
	struct netif ifc;
	struct init_block *init;
	volatile struct desc *rx, *tx;
	uint8_t *rx_buf[RX_COUNT], *tx_buf[TX_COUNT];
	unsigned rx_next, tx_next;
	struct mutex tx_lock;
} card = { .tx_lock = MUTEX_INIT };

#define barrier() __asm__ volatile("" ::: "memory")

/* Register access; the RAP/data pair must not be split, so interrupts
 * (whose handler uses RAP too) are off around it. */
static uint32_t csr_read(unsigned n)
{
	uint32_t flags = cpu_irq_save(), v;
	if (card.dwio) {
		outl(card.io + DWIO_RAP, n);
		v = inl(card.io + DWIO_RDP) & 0xFFFF;
	} else {
		outw(card.io + WIO_RAP, (uint16_t)n);
		v = inw(card.io + WIO_RDP);
	}
	cpu_irq_restore(flags);
	return v;
}

static void csr_write(unsigned n, uint32_t v)
{
	uint32_t flags = cpu_irq_save();
	if (card.dwio) {
		outl(card.io + DWIO_RAP, n);
		outl(card.io + DWIO_RDP, v);
	} else {
		outw(card.io + WIO_RAP, (uint16_t)n);
		outw(card.io + WIO_RDP, (uint16_t)v);
	}
	cpu_irq_restore(flags);
}

static void bcr_write(unsigned n, uint32_t v)
{
	uint32_t flags = cpu_irq_save();
	if (card.dwio) {
		outl(card.io + DWIO_RAP, n);
		outl(card.io + DWIO_BDP, v);
	} else {
		outw(card.io + WIO_RAP, (uint16_t)n);
		outw(card.io + WIO_BDP, (uint16_t)v);
	}
	cpu_irq_restore(flags);
}

static uint32_t bcr_read(unsigned n)
{
	uint32_t flags = cpu_irq_save(), v;
	if (card.dwio) {
		outl(card.io + DWIO_RAP, n);
		v = inl(card.io + DWIO_BDP) & 0xFFFF;
	} else {
		outw(card.io + WIO_RAP, (uint16_t)n);
		v = inw(card.io + WIO_BDP);
	}
	cpu_irq_restore(flags);
	return v;
}

/* Resets the chip and finds out which I/O width it answers in: after a
 * reset CSR0 reads STOP, and RAP reads back what was written. Firmware
 * may have left it in either mode (only a hardware reset clears DWIO). */
static bool reset_and_detect(void)
{
	inl(card.io + DWIO_RESET);
	inw(card.io + WIO_RESET);
	timer_sleep_ms(1);
	card.dwio = false;
	outw(card.io + WIO_RAP, 88);
	if (inw(card.io + WIO_RAP) == 88 && csr_read(0) == CSR0_STOP) {
		return true;
	}
	inl(card.io + DWIO_RESET);
	timer_sleep_ms(1);
	card.dwio = true;
	outl(card.io + DWIO_RAP, 88);
	return (inl(card.io + DWIO_RAP) & 0xFFFF) == 88 && csr_read(0) == CSR0_STOP;
}

static uint32_t buffer_phys(uintptr_t page_phys, unsigned i)
{
	return (uint32_t)(page_phys + (i % BUFS_PER_PAGE) * 2048);
}

/* The rings and buffers, in whole pages whose physical addresses the
 * chip is given. */
static bool allocate(uint32_t *init_phys)
{
	uintptr_t phys, buf_phys = 0;
	uint8_t *page = kpage_alloc(&phys), *buf_page = 0;
	if (!page) {
		return false;
	}
	card.init = (struct init_block *)page;
	card.rx = (volatile struct desc *)(page + 512);  /* 32 x 16 bytes */
	card.tx = (volatile struct desc *)(page + 1024); /* 8 x 16 */
	*init_phys = (uint32_t)phys;
	card.init->rdra = (uint32_t)phys + 512;
	card.init->tdra = (uint32_t)phys + 1024;
	for (unsigned i = 0; i < RX_COUNT + TX_COUNT; i++) {
		if (i % BUFS_PER_PAGE == 0 && !(buf_page = kpage_alloc(&buf_phys))) {
			return false;
		}
		uint8_t *buf = buf_page + (i % BUFS_PER_PAGE) * 2048;
		uint32_t addr = buffer_phys(buf_phys, i);
		if (i < RX_COUNT) {
			card.rx_buf[i] = buf;
			card.rx[i].addr = addr;
			card.rx[i].misc = 0;
			card.rx[i].flags = DESC_OWN | DESC_ONES | ((uint32_t)-BUF_SIZE & 0x0FFF);
		} else {
			unsigned t = i - RX_COUNT;
			card.tx_buf[t] = buf;
			card.tx[t].addr = addr;
			card.tx[t].flags = 0; /* ours */
		}
	}
	return true;
}

static void pcnet_irq(void)
{
	uint32_t csr0 = csr_read(0);
	if (!(csr0 & CSR0_INTR)) {
		return; /* another device on this line */
	}
	csr_write(0, (csr0 & CSR0_ACK) | CSR0_IENA);
	if (csr0 & CSR0_MISS) {
		card.ifc.rx_dropped++;
	}
	if (csr0 & CSR0_RINT) {
		netif_notify();
	}
}

static void pcnet_poll(struct netif *ifc)
{
	for (;;) {
		volatile struct desc *d = &card.rx[card.rx_next];
		uint32_t flags = d->flags;
		if (flags & DESC_OWN) {
			break;
		}
		barrier();
		/* The card can hand a descriptor back before it is done with
		 * it: QEMU's writes it twice, the start of the frame (STP)
		 * first, then its end (ENP) and length. While the descriptor
		 * lacks them and the card still owns the next one, it is being
		 * written: the interrupt that ends the frame will bring us
		 * back. (Once the next one is ours too, the card has moved on,
		 * and a frame too big for one buffer is dropped below.) */
		if (!(flags & DESC_ERR) && (!(flags & DESC_ENP) || !(d->misc & 0x0FFF))
		    && (card.rx[(card.rx_next + 1) % RX_COUNT].flags & DESC_OWN)) {
			break;
		}
		barrier();
		flags = d->flags;
		size_t len = d->misc & 0x0FFF; /* with the 4-byte FCS */
		if ((flags & (DESC_ERR | DESC_STP | DESC_ENP)) == (DESC_STP | DESC_ENP) && len > 4
		    && len - 4 <= ETH_FRAME_MAX) {
			netif_input(ifc, card.rx_buf[card.rx_next], len - 4);
		} else {
			ifc->rx_dropped++;
		}
		d->misc = 0;
		barrier();
		d->flags = DESC_OWN | DESC_ONES | ((uint32_t)-BUF_SIZE & 0x0FFF);
		card.rx_next = (card.rx_next + 1) % RX_COUNT;
	}
}

static int pcnet_transmit(struct netif *ifc, const uint8_t *frame, size_t len)
{
	if (len > ETH_FRAME_MAX) {
		return -EINVAL;
	}
	mutex_lock(&card.tx_lock);
	volatile struct desc *d = &card.tx[card.tx_next];
	/* The slot is free once the card has sent what was in it before. */
	for (int waited = 0; d->flags & DESC_OWN; waited++) {
		if (waited == 100) {
			mutex_unlock(&card.tx_lock);
			ifc->tx_errors++;
			return -ETIMEDOUT;
		}
		timer_sleep_ms(1);
	}
	if (d->flags & DESC_ERR) {
		ifc->tx_errors++; /* that earlier frame */
	}
	uint8_t *buf = card.tx_buf[card.tx_next];
	memcpy(buf, frame, len);
	if (len < ETH_FRAME_MIN) {
		memset(buf + len, 0, ETH_FRAME_MIN - len);
		len = ETH_FRAME_MIN;
	}
	d->misc = 0;
	barrier();
	d->flags = DESC_OWN | DESC_STP | DESC_ENP | DESC_ONES | ((uint32_t)-len & 0x0FFF);
	barrier();
	csr_write(0, CSR0_TDMD | CSR0_IENA);
	card.tx_next = (card.tx_next + 1) % TX_COUNT;
	mutex_unlock(&card.tx_lock);
	return 0; /* the stack counts frames sent */
}

void pcnet_probe(void)
{
	const struct pci_device *pci = pci_find(PCNET_VENDOR, PCNET_DEVICE);
	uint16_t io = pci ? (uint16_t)pci_bar_io(pci, 0) : 0;
	if (!io) {
		return;
	}
	if (pci->irq == 0 || pci->irq >= 16) {
		kprintf("pcn0: PCnet at pci %02x:%02x.%x has no interrupt line\n", pci->bus, pci->dev,
		        pci->fn);
		return;
	}
	pci_enable(pci);
	card.io = io;
	uint32_t init_phys;
	if (!reset_and_detect()) {
		kprintf("pcn0: PCnet at pci %02x:%02x.%x doesn't answer\n", pci->bus, pci->dev, pci->fn);
		return;
	}
	if (!allocate(&init_phys)) {
		kprintf("pcn0: out of memory\n");
		return;
	}
	for (int i = 0; i < ETH_ALEN; i++) {
		card.ifc.mac[i] = inb(io + APROM + i);
		card.init->padr[i] = card.ifc.mac[i];
	}
	card.init->mode_lengths = 5u << 20 | 3u << 28; /* normal mode; 32 RX, 8 TX */

	bcr_write(20, BCR20_SWSTYLE2); /* 32-bit structures */
	(void)bcr_read(20);
	csr_write(1, init_phys & 0xFFFF);
	csr_write(2, init_phys >> 16);
	csr_write(3, CSR3_IDONM | CSR3_TINTM); /* interrupts for receiving and errors */
	csr_write(4, csr_read(4) | CSR4_APAD_XMT);
	csr_write(0, CSR0_INIT);
	for (int waited = 0; !(csr_read(0) & CSR0_IDON); waited++) {
		if (waited == 100) {
			kprintf("pcn0: the card didn't take its initialization block\n");
			return;
		}
		timer_sleep_ms(1);
	}
	csr_write(0, CSR0_IDON | CSR0_STRT | CSR0_IENA);

	strlcpy(card.ifc.name, "pcn0", sizeof(card.ifc.name));
	card.ifc.transmit = pcnet_transmit;
	card.ifc.poll = pcnet_poll;
	card.ifc.driver = &card;
	irq_install_handler(pci->irq, pcnet_irq);
	netif_register(&card.ifc);
	const uint8_t *m = card.ifc.mac;
	kprintf("pcn0: AMD PCnet at pci %02x:%02x.%x, io 0x%x irq %d, %02x:%02x:%02x:%02x:%02x:%02x\n",
	        pci->bus, pci->dev, pci->fn, io, pci->irq, m[0], m[1], m[2], m[3], m[4], m[5]);
}
