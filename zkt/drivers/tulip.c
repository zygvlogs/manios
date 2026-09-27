/* DEC 21143 "Tulip" Fast Ethernet (and the 21140 and clones of the
 * family; QEMU's -device tulip), written from Digital's 21143 PCI/CardBus
 * 10/100-Mb/s Ethernet LAN Controller Hardware Reference Manual.
 *
 * The control and status registers (CSRs) are in I/O space (BAR 0),
 * eight bytes apart. Frames move through rings of 16-byte descriptors in
 * memory, owned by the chip while their OWN bit is set; the driver sets
 * it to give a descriptor over and writes CSR1/CSR2 ("poll demand") to
 * make the chip look. The Ethernet address is in the serial ROM (a
 * 93C46-type EEPROM behind CSR9, read by driving its clock and data
 * lines), and the address filter is loaded by transmitting a "setup
 * frame": sixteen addresses the chip then accepts -- ours and the
 * broadcast address. */
#include "tulip.h"
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

#define CSR(n) ((uint16_t)((n) * 8))
#define CSR0_SWR  (1u << 0)  /* software reset */
#define CSR5_TI   (1u << 0)
#define CSR5_TU   (1u << 2)
#define CSR5_RI   (1u << 6)
#define CSR5_RU   (1u << 7)  /* no receive descriptor free */
#define CSR5_AIS  (1u << 15)
#define CSR5_NIS  (1u << 16)
#define CSR5_ALL  0x0001FFFFu
#define CSR6_SR   (1u << 1)  /* start receiving */
#define CSR6_ST   (1u << 13) /* start transmitting */
#define CSR6_SF   (1u << 21) /* store and forward */
#define CSR7_TIE  (1u << 0)
#define CSR7_RIE  (1u << 6)
#define CSR7_RUE  (1u << 7)
#define CSR7_AIE  (1u << 15)
#define CSR7_NIE  (1u << 16)

/* CSR9's serial ROM lines. */
#define SROM_CS  0x01
#define SROM_CLK 0x02
#define SROM_DI  0x04 /* to the ROM */
#define SROM_DO  0x08 /* from the ROM */
#define SROM_SEL 0x0800
#define SROM_RD  0x4000
#define SROM_READ 6 /* start bit and the READ opcode, 110 */
#define SROM_MAC_WORD 10 /* where the Ethernet address is (SROM format 3 and 4) */

#define DESC_OWN (1u << 31)
#define RDES0_ES (1u << 15) /* error summary */
#define RDES0_FS (1u << 9)
#define RDES0_LS (1u << 8)
#define DES1_ER  (1u << 25) /* end of ring */
#define TDES1_IC (1u << 31)
#define TDES1_LS (1u << 30)
#define TDES1_FS (1u << 29)
#define TDES1_SET (1u << 27) /* a setup frame */
#define TDES0_ES (1u << 15)

#define RX_COUNT 16
#define TX_COUNT 8
#define BUF_SIZE 1536
#define SETUP_FRAME 192

struct desc {
	volatile uint32_t status, control, buffer1, buffer2;
};

static struct {
	uint16_t io;
	struct netif ifc;
	struct desc *rx, *tx;
	uint8_t *rx_buf[RX_COUNT], *tx_buf[TX_COUNT];
	uintptr_t rx_phys[RX_COUNT], tx_phys[TX_COUNT];
	unsigned rx_next, tx_next;
	struct mutex tx_lock;
} card = { .tx_lock = MUTEX_INIT };

#define barrier() __asm__ volatile("" ::: "memory")

static void srom_out(uint32_t v)
{
	outl(card.io + CSR(9), v);
	for (int i = 0; i < 4; i++) {
		(void)inl(card.io + CSR(9)); /* the ROM wants ~250 ns per edge */
	}
}

/* One 16-bit word of the serial ROM, 6-bit addresses (a 93C46). */
static uint16_t srom_word(uint8_t address)
{
	uint32_t on = SROM_SEL | SROM_RD | SROM_CS;
	srom_out(SROM_SEL | SROM_RD);
	srom_out(on);
	/* The start bit, READ, and the address: nine bits, first first. */
	uint32_t command = (uint32_t)SROM_READ << 6 | (address & 0x3F);
	for (int bit = 8; bit >= 0; bit--) {
		uint32_t di = (command >> bit) & 1 ? SROM_DI : 0;
		srom_out(on | di);
		srom_out(on | di | SROM_CLK);
	}
	srom_out(on);
	uint16_t word = 0;
	for (int bit = 0; bit < 16; bit++) {
		srom_out(on | SROM_CLK);
		word = (uint16_t)(word << 1 | ((inl(card.io + CSR(9)) & SROM_DO) ? 1 : 0));
		srom_out(on);
	}
	srom_out(SROM_SEL | SROM_RD); /* deselect */
	return word;
}

static void tulip_irq(void)
{
	uint32_t csr5 = inl(card.io + CSR(5));
	if (!(csr5 & (CSR5_NIS | CSR5_AIS))) {
		return; /* not this card */
	}
	outl(card.io + CSR(5), csr5 & CSR5_ALL); /* write 1s to clear */
	if (csr5 & CSR5_RU) {
		card.ifc.rx_dropped++;
	}
	if (csr5 & (CSR5_RI | CSR5_RU)) {
		netif_notify();
	}
}

static void tulip_poll(struct netif *ifc)
{
	bool gave_back = false;
	for (;;) {
		struct desc *d = &card.rx[card.rx_next];
		uint32_t status = d->status;
		if (status & DESC_OWN) {
			break;
		}
		barrier();
		uint32_t length = (status >> 16) & 0x3FFF; /* with the 4-byte CRC */
		if (!(status & RDES0_ES) && (status & RDES0_FS) && (status & RDES0_LS) && length >= 4 + 14
		    && length - 4 <= ETH_FRAME_MAX) {
			netif_input(ifc, card.rx_buf[card.rx_next], length - 4);
		} else {
			ifc->rx_dropped++;
		}
		barrier();
		d->status = DESC_OWN;
		gave_back = true;
		card.rx_next = (card.rx_next + 1) % RX_COUNT;
	}
	if (gave_back) {
		outl(card.io + CSR(2), 1); /* receive poll demand, if it had stopped */
	}
}

/* Queues one frame (or setup frame) in the next transmit slot; caller
 * holds tx_lock. */
static int send(const uint8_t *data, size_t len, uint32_t flags)
{
	struct desc *d = &card.tx[card.tx_next];
	for (int waited = 0; d->status & DESC_OWN; waited++) {
		if (waited == 100) {
			return -ETIMEDOUT;
		}
		timer_sleep_ms(1);
	}
	if (d->status & TDES0_ES) {
		card.ifc.tx_errors++; /* that earlier frame */
	}
	memcpy(card.tx_buf[card.tx_next], data, len);
	d->buffer1 = (uint32_t)card.tx_phys[card.tx_next];
	d->control = (d->control & DES1_ER) | flags | (uint32_t)len;
	barrier();
	d->status = DESC_OWN;
	barrier();
	outl(card.io + CSR(1), 1); /* transmit poll demand */
	card.tx_next = (card.tx_next + 1) % TX_COUNT;
	return 0;
}

static int tulip_transmit(struct netif *ifc, const uint8_t *frame, size_t len)
{
	uint8_t padded[ETH_FRAME_MIN];
	if (len > ETH_FRAME_MAX) {
		return -EINVAL;
	}
	if (len < ETH_FRAME_MIN) {
		memcpy(padded, frame, len);
		memset(padded + len, 0, ETH_FRAME_MIN - len);
		frame = padded;
		len = ETH_FRAME_MIN;
	}
	mutex_lock(&card.tx_lock);
	int rc = send(frame, len, TDES1_FS | TDES1_LS);
	mutex_unlock(&card.tx_lock);
	if (rc) {
		ifc->tx_errors++;
	}
	return rc;
}

/* The setup frame for perfect filtering: 16 addresses of three 16-bit
 * words, each word in the low half of a 32-bit longword. Ours, then the
 * broadcast address in the other fifteen. */
static int load_filter(void)
{
	uint8_t frame[SETUP_FRAME];
	memset(frame, 0, sizeof(frame));
	for (int entry = 0; entry < 16; entry++) {
		for (int w = 0; w < 3; w++) {
			uint8_t *p = frame + entry * 12 + w * 4;
			p[0] = entry == 0 ? card.ifc.mac[2 * w] : 0xFF;
			p[1] = entry == 0 ? card.ifc.mac[2 * w + 1] : 0xFF;
		}
	}
	mutex_lock(&card.tx_lock);
	int rc = send(frame, sizeof(frame), TDES1_SET);
	mutex_unlock(&card.tx_lock);
	return rc;
}

static bool allocate(uintptr_t *rx_ring, uintptr_t *tx_ring)
{
	uintptr_t phys;
	uint8_t *page = dma_alloc(1024, false, &phys);
	if (!page) {
		return false;
	}
	card.rx = (struct desc *)page;
	card.tx = (struct desc *)(page + 512);
	*rx_ring = phys;
	*tx_ring = phys + 512;
	for (int i = 0; i < RX_COUNT + TX_COUNT; i += 2) {
		uintptr_t buf_phys;
		uint8_t *buf = dma_alloc(2 * 2048, false, &buf_phys);
		if (!buf) {
			return false;
		}
		for (int j = 0; j < 2; j++) {
			int k = i + j;
			uint8_t *b = buf + j * 2048;
			uintptr_t bp = buf_phys + (uintptr_t)j * 2048;
			if (k < RX_COUNT) {
				card.rx_buf[k] = b;
				card.rx_phys[k] = bp;
				card.rx[k].buffer1 = (uint32_t)bp;
				card.rx[k].control = BUF_SIZE | (k == RX_COUNT - 1 ? DES1_ER : 0);
				card.rx[k].status = DESC_OWN;
			} else {
				k -= RX_COUNT;
				card.tx_buf[k] = b;
				card.tx_phys[k] = bp;
				card.tx[k].control = k == TX_COUNT - 1 ? DES1_ER : 0;
			}
		}
	}
	return true;
}

static const struct pci_device *find(void)
{
	/* 21143, 21140 and 21041: the same descriptors and CSRs. */
	static const uint16_t DEVICES[] = { 0x0019, 0x0009, 0x0014 };
	for (size_t i = 0; i < sizeof(DEVICES) / sizeof(DEVICES[0]); i++) {
		const struct pci_device *d = pci_find(0x1011, DEVICES[i]);
		if (d) {
			return d;
		}
	}
	return 0;
}

void tulip_probe(void)
{
	const struct pci_device *pci = find();
	uint16_t io = pci ? (uint16_t)pci_bar_io(pci, 0) : 0;
	if (!io) {
		return;
	}
	if (pci->irq == 0 || pci->irq >= 16) {
		kprintf("dc0: 21143 at pci %02x:%02x.%x has no interrupt line\n", pci->bus, pci->dev,
		        pci->fn);
		return;
	}
	pci_enable(pci);
	card.io = io;
	outl(io + CSR(0), CSR0_SWR);
	timer_sleep_ms(1);
	outl(io + CSR(0), 0);
	uint8_t *mac = card.ifc.mac;
	uint32_t sum = 0;
	for (int w = 0; w < 3; w++) {
		uint16_t v = srom_word((uint8_t)(SROM_MAC_WORD + w));
		mac[2 * w] = (uint8_t)v;
		mac[2 * w + 1] = (uint8_t)(v >> 8);
		sum += v;
	}
	if (sum == 0 || sum == 3 * 0xFFFFu) {
		kprintf("dc0: no Ethernet address in the serial ROM\n");
		return;
	}
	uintptr_t rx_ring, tx_ring;
	if (!allocate(&rx_ring, &tx_ring)) {
		kprintf("dc0: out of memory\n");
		return;
	}
	outl(io + CSR(3), (uint32_t)rx_ring);
	outl(io + CSR(4), (uint32_t)tx_ring);
	outl(io + CSR(5), CSR5_ALL);
	strlcpy(card.ifc.name, "dc0", sizeof(card.ifc.name));
	card.ifc.transmit = tulip_transmit;
	card.ifc.poll = tulip_poll;
	card.ifc.driver = &card;
	irq_install_handler(pci->irq, tulip_irq);
	outl(io + CSR(7), CSR7_NIE | CSR7_AIE | CSR7_RIE | CSR7_RUE);
	outl(io + CSR(6), CSR6_SF | CSR6_ST);
	if (load_filter() != 0) {
		kprintf("dc0: the address filter didn't load\n");
	}
	outl(io + CSR(6), CSR6_SF | CSR6_ST | CSR6_SR);
	netif_register(&card.ifc);
	const char *model = pci->device == 0x0019 ? "21143" : pci->device == 0x0009 ? "21140" : "21041";
	kprintf("dc0: DEC %s (Tulip) at pci %02x:%02x.%x, irq %d, %02x:%02x:%02x:%02x:%02x:%02x\n",
	        model, pci->bus, pci->dev, pci->fn, pci->irq, mac[0], mac[1], mac[2], mac[3], mac[4],
	        mac[5]);
}
