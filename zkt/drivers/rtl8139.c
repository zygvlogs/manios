/* RealTek RTL8139 Fast Ethernet (the 8139C/D and the cards built on
 * it; QEMU's -device rtl8139), written from RealTek's RTL8139(A/B/C/D)
 * data sheet and programming guide. Registers in I/O space (BAR 0).
 *
 * Receive: one ring buffer of 8 KiB (plus room for a frame to run past
 * its end, the WRAP mode), which the card fills with frames, each after
 * a 4-byte header (status, length with the CRC); the driver reads from
 * CAPR on and moves CAPR past each frame -- 16 bytes behind where it
 * reads, as the card counts it. Transmit: four slots, used in turn, each
 * with a buffer whose address and length go in the slot's registers. */
#include "rtl8139.h"
#include <stdbool.h>
#include "dma.h"
#include "io.h"
#include "irq.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "mutex.h"
#include "net.h"
#include "pci.h"
#include "timer.h"

#define REG_IDR0    0x00 /* the Ethernet address */
#define REG_MAR0    0x08 /* the multicast filter */
#define REG_TSD0    0x10 /* transmit status, per slot (4 bytes apart) */
#define REG_TSAD0   0x20 /* transmit start address, per slot */
#define REG_RBSTART 0x30
#define REG_CR      0x37
#define REG_CAPR    0x38
#define REG_IMR     0x3C
#define REG_ISR     0x3E
#define REG_TCR     0x40
#define REG_RCR     0x44
#define REG_MPC     0x4C /* frames missed */
#define REG_9346CR  0x50
#define REG_CONFIG1 0x52
#define REG_MSR     0x58

#define CR_BUFE 0x01 /* the receive buffer is empty */
#define CR_TE   0x04
#define CR_RE   0x08
#define CR_RST  0x10
#define ISR_ROK   0x0001
#define ISR_RER   0x0002
#define ISR_TOK   0x0004
#define ISR_TER   0x0008
#define ISR_RXOVW 0x0010
#define ISR_FOVW  0x0040
#define ISR_RX    (ISR_ROK | ISR_RER | ISR_RXOVW | ISR_FOVW)
#define TSD_OWN   (1u << 13) /* the card has taken the frame */
#define TSD_TOK   (1u << 15)
#define TSD_TUN   (1u << 14)
#define TSD_TABT  (1u << 30)
#define RCR_APM   (1u << 1)  /* frames to our address */
#define RCR_AM    (1u << 2)  /* multicast, as the filter says */
#define RCR_AB    (1u << 3)  /* broadcasts */
#define RCR_WRAP  (1u << 7)  /* a frame may run past the ring's end */
#define RCR_MXDMA (7u << 8)  /* DMA bursts unlimited */
#define RCR_RXFTH (7u << 13) /* whole frames to memory */
#define TCR_MXDMA (7u << 8)  /* 2048-byte bursts */
#define TCR_IFG   (3u << 24) /* the standard gap between frames */
#define MSR_LINKB 0x04       /* set: no link */
#define RX_HEADER_ROK 0x0001

#define RX_RING  8192
#define RX_BYTES (RX_RING + 16 + 1536)
#define TX_SLOTS 4
#define TX_BYTES 1536

static struct {
	uint16_t io;
	struct netif ifc;
	uint8_t *rx;
	uint32_t rx_offset;
	uint8_t *tx[TX_SLOTS];
	uintptr_t tx_phys[TX_SLOTS];
	unsigned tx_next;
	struct mutex tx_lock;
} card = { .tx_lock = MUTEX_INIT };

static uintptr_t rx_phys;

/* Resets the card and starts it receiving into the ring from its start;
 * false if it doesn't come out of reset. */
static bool start(void)
{
	uint16_t io = card.io;
	outb(io + REG_CR, CR_RST);
	for (int i = 0; i < 100000 && (inb(io + REG_CR) & CR_RST); i++) {
	}
	if (inb(io + REG_CR) & CR_RST) {
		return false;
	}
	outl(io + REG_RBSTART, (uint32_t)rx_phys);
	outl(io + REG_MAR0, 0); /* no multicast */
	outl(io + REG_MAR0 + 4, 0);
	outb(io + REG_CR, CR_TE | CR_RE); /* before the configuration, as the guide says */
	outl(io + REG_RCR, RCR_APM | RCR_AM | RCR_AB | RCR_WRAP | RCR_MXDMA | RCR_RXFTH);
	outl(io + REG_TCR, TCR_MXDMA | TCR_IFG);
	outw(io + REG_CAPR, (uint16_t)(RX_RING - 16));
	outl(io + REG_MPC, 0);
	card.rx_offset = 0;
	outw(io + REG_ISR, 0xFFFF);
	outw(io + REG_IMR, ISR_RX | ISR_TOK | ISR_TER);
	return true;
}

static void rtl_irq(void)
{
	uint16_t isr = inw(card.io + REG_ISR);
	if (!isr || isr == 0xFFFF) {
		return; /* another device on the line */
	}
	outw(card.io + REG_ISR, isr); /* write 1s to clear */
	if (isr & (ISR_RXOVW | ISR_FOVW)) {
		card.ifc.rx_dropped++;
	}
	if (isr & ISR_RX) {
		netif_notify();
	}
}

static void rtl_poll(struct netif *ifc)
{
	while (!(inb(card.io + REG_CR) & CR_BUFE)) {
		uint8_t *p = card.rx + card.rx_offset;
		uint16_t status = (uint16_t)(p[0] | p[1] << 8);
		uint16_t length = (uint16_t)(p[2] | p[3] << 8); /* with the 4-byte CRC */
		if (!(status & RX_HEADER_ROK) || length < 4 || length > RX_RING / 2) {
			/* A header that makes no sense: the ring can't be trusted.
			 * Reset the card, whose transmit slots start over too. */
			ifc->rx_dropped++;
			mutex_lock(&card.tx_lock);
			start();
			card.tx_next = 0;
			mutex_unlock(&card.tx_lock);
			break;
		}
		if (length < 4 + 14 || length > ETH_FRAME_MAX + 4) {
			ifc->rx_dropped++; /* too short or too long for Ethernet */
		} else {
			netif_input(ifc, p + 4, length - 4u);
		}
		card.rx_offset = ((card.rx_offset + length + 4 + 3) & ~3u) % RX_RING;
		outw(card.io + REG_CAPR, (uint16_t)(card.rx_offset - 16));
	}
}

static int rtl_transmit(struct netif *ifc, const uint8_t *frame, size_t len)
{
	if (len > ETH_FRAME_MAX) {
		return -EINVAL;
	}
	mutex_lock(&card.tx_lock);
	unsigned slot = card.tx_next;
	uint16_t tsd = (uint16_t)(REG_TSD0 + 4 * slot);
	/* A slot is free once the card has sent (or given up on) its frame. */
	for (int waited = 0;; waited++) {
		uint32_t status = inl(card.io + tsd);
		if (status & (TSD_TOK | TSD_TABT | TSD_TUN) || !(status & 0x1FFF)) {
			if (status & (TSD_TABT | TSD_TUN)) {
				ifc->tx_errors++;
			}
			break;
		}
		if (waited == 100) {
			mutex_unlock(&card.tx_lock);
			ifc->tx_errors++;
			return -ETIMEDOUT;
		}
		timer_sleep_ms(1);
	}
	memcpy(card.tx[slot], frame, len);
	if (len < ETH_FRAME_MIN) {
		memset(card.tx[slot] + len, 0, ETH_FRAME_MIN - len);
		len = ETH_FRAME_MIN;
	}
	outl(card.io + REG_TSAD0 + 4 * slot, (uint32_t)card.tx_phys[slot]);
	outl(card.io + tsd, (uint32_t)len); /* OWN cleared: the card sends it */
	card.tx_next = (slot + 1) % TX_SLOTS;
	mutex_unlock(&card.tx_lock);
	return 0;
}

void rtl8139_probe(void)
{
	const struct pci_device *pci = pci_find(0x10EC, 0x8139);
	uint16_t io = pci ? (uint16_t)pci_bar_io(pci, 0) : 0;
	if (!io) {
		return;
	}
	if (pci->irq == 0 || pci->irq >= 16) {
		kprintf("rl0: RTL8139 at pci %02x:%02x.%x has no interrupt line\n", pci->bus, pci->dev,
		        pci->fn);
		return;
	}
	pci_enable(pci);
	card.io = io;
	outb(io + REG_CONFIG1, 0); /* out of low-power mode */
	card.rx = dma_alloc(RX_BYTES, false, &rx_phys);
	for (int i = 0; i < TX_SLOTS && card.rx; i += 2) {
		uintptr_t phys;
		uint8_t *page = dma_alloc(2 * TX_BYTES, false, &phys);
		if (!page) {
			card.rx = 0;
			break;
		}
		for (int j = 0; j < 2; j++) {
			card.tx[i + j] = page + j * 2048;
			card.tx_phys[i + j] = phys + (uintptr_t)j * 2048;
		}
	}
	if (!card.rx) {
		kprintf("rl0: out of memory\n");
		return;
	}
	uint8_t *mac = card.ifc.mac;
	for (int i = 0; i < ETH_ALEN; i++) {
		mac[i] = inb(io + REG_IDR0 + i);
	}
	if (!start()) {
		kprintf("rl0: the card doesn't come out of reset\n");
		return;
	}
	strlcpy(card.ifc.name, "rl0", sizeof(card.ifc.name));
	card.ifc.transmit = rtl_transmit;
	card.ifc.poll = rtl_poll;
	card.ifc.driver = &card;
	irq_install_handler(pci->irq, rtl_irq);
	netif_register(&card.ifc);
	kprintf("rl0: RealTek 8139 at pci %02x:%02x.%x, irq %d, %02x:%02x:%02x:%02x:%02x:%02x, "
	        "link %s\n", pci->bus, pci->dev, pci->fn, pci->irq, mac[0], mac[1], mac[2], mac[3],
	        mac[4], mac[5], (inb(io + REG_MSR) & MSR_LINKB) ? "down" : "up");
}
