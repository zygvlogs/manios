/* Virtio network cards (QEMU's -device virtio-net-pci and other
 * hypervisors' paravirtual NICs), written from the virtio specification's
 * network device section, over the legacy PCI interface (virtio.h).
 *
 * Queue 0 receives, queue 1 transmits. Every frame is preceded by a
 * 10-byte virtio-net header (no offloads are asked for, so it is all
 * zeros going out and ignored coming in), in a descriptor of its own, as
 * legacy devices without ANY_LAYOUT expect. Receive buffers are posted
 * in advance and posted again once their frame is handed on; transmit
 * buffers are reused once the device has returned them. The device
 * interrupts for received frames; reading its ISR register acknowledges. */
#include "virtio_net.h"
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
#include "virtio.h"

#define DEVICE_LEGACY_NET 0x1000
#define FEATURE_MAC    (1u << 5)
#define FEATURE_STATUS (1u << 16)
#define STATUS_LINK_UP 1
#define HEADER 10
#define RX_BUFFERS 16
#define TX_BUFFERS 8
#define BUF_SIZE 2048 /* HEADER, then up to a whole frame */

static struct {
	uint16_t io;
	bool status;
	struct netif ifc;
	struct virtq rx, tx;
	uint8_t *rx_buf[RX_BUFFERS], *tx_buf[TX_BUFFERS];
	uintptr_t rx_phys[RX_BUFFERS], tx_phys[TX_BUFFERS];
	bool tx_busy[TX_BUFFERS];
	struct mutex tx_lock;
} card = { .tx_lock = MUTEX_INIT };

/* Buffer i's chain: descriptors 2i (the header) and 2i + 1 (the frame). */
static void chain(struct virtq *q, int i, uintptr_t phys, uint32_t frame_len, bool device_writes)
{
	uint16_t w = device_writes ? VRING_DESC_F_WRITE : 0;
	q->desc[2 * i].addr = phys;
	q->desc[2 * i].len = HEADER;
	q->desc[2 * i].flags = (uint16_t)(w | VRING_DESC_F_NEXT);
	q->desc[2 * i].next = (uint16_t)(2 * i + 1);
	q->desc[2 * i + 1].addr = phys + HEADER;
	q->desc[2 * i + 1].len = frame_len;
	q->desc[2 * i + 1].flags = w;
	q->desc[2 * i + 1].next = 0;
}

static void post_rx(int i)
{
	chain(&card.rx, i, card.rx_phys[i], BUF_SIZE - HEADER, true);
	virtq_submit(card.io, 0, &card.rx, (uint16_t)(2 * i));
}

static void vnet_irq(void)
{
	uint8_t isr = inb(card.io + VIRTIO_ISR); /* reading acknowledges */
	if (isr & 1) {
		netif_notify();
	}
}

static void vnet_poll(struct netif *ifc)
{
	struct vring_used_elem e;
	while (virtq_used(&card.rx, &e)) {
		int i = (int)(e.id / 2);
		if (i >= RX_BUFFERS) {
			continue;
		}
		if (e.len > HEADER && e.len - HEADER <= ETH_FRAME_MAX) {
			netif_input(ifc, card.rx_buf[i] + HEADER, e.len - HEADER);
		} else {
			ifc->rx_dropped++;
		}
		post_rx(i);
	}
}

static int vnet_transmit(struct netif *ifc, const uint8_t *frame, size_t len)
{
	if (len > ETH_FRAME_MAX) {
		return -EINVAL;
	}
	mutex_lock(&card.tx_lock);
	int slot = -1;
	for (int waited = 0; slot < 0; waited++) {
		struct vring_used_elem e;
		while (virtq_used(&card.tx, &e)) {
			if (e.id / 2 < TX_BUFFERS) {
				card.tx_busy[e.id / 2] = false;
			}
		}
		for (int i = 0; i < TX_BUFFERS && slot < 0; i++) {
			if (!card.tx_busy[i]) {
				slot = i;
			}
		}
		if (slot < 0) {
			if (waited == 100) {
				mutex_unlock(&card.tx_lock);
				ifc->tx_errors++;
				return -ETIMEDOUT;
			}
			timer_sleep_ms(1);
		}
	}
	memset(card.tx_buf[slot], 0, HEADER);
	memcpy(card.tx_buf[slot] + HEADER, frame, len);
	card.tx_busy[slot] = true;
	chain(&card.tx, slot, card.tx_phys[slot], (uint32_t)len, false);
	virtq_submit(card.io, 1, &card.tx, (uint16_t)(2 * slot));
	mutex_unlock(&card.tx_lock);
	return 0;
}

static bool buffers(uint8_t **bufs, uintptr_t *phys, int count)
{
	for (int i = 0; i < count; i += 2) {
		uintptr_t p;
		uint8_t *page = dma_alloc(2 * BUF_SIZE, false, &p);
		if (!page) {
			return false;
		}
		for (int j = 0; j < 2 && i + j < count; j++) {
			bufs[i + j] = page + j * BUF_SIZE;
			phys[i + j] = p + (uintptr_t)j * BUF_SIZE;
		}
	}
	return true;
}

void virtio_net_probe(void)
{
	const struct pci_device *pci = pci_find(VIRTIO_VENDOR, DEVICE_LEGACY_NET);
	if (!pci) {
		return;
	}
	if (pci->irq == 0 || pci->irq >= 16) {
		kprintf("vio0: virtio-net at pci %02x:%02x.%x has no interrupt line\n", pci->bus,
		        pci->dev, pci->fn);
		return;
	}
	uint16_t io = virtio_start(pci);
	if (!io) {
		return;
	}
	card.io = io;
	uint32_t features = virtio_features(io, FEATURE_MAC | FEATURE_STATUS);
	card.status = features & FEATURE_STATUS;
	if (!virtio_queue(io, 0, &card.rx) || !virtio_queue(io, 1, &card.tx)
	    || card.rx.size < 2 * RX_BUFFERS || card.tx.size < 2 * TX_BUFFERS
	    || !buffers(card.rx_buf, card.rx_phys, RX_BUFFERS)
	    || !buffers(card.tx_buf, card.tx_phys, TX_BUFFERS)) {
		outb(io + VIRTIO_STATUS, VIRTIO_STATUS_FAILED);
		kprintf("vio0: cannot set up the virtio network card\n");
		return;
	}
	uint8_t *mac = card.ifc.mac;
	for (int i = 0; i < ETH_ALEN; i++) {
		/* Without the MAC feature the device has none: a local one. */
		mac[i] = (features & FEATURE_MAC) ? inb(io + VIRTIO_CONFIG + i)
		                                  : (uint8_t[]){ 0x02, 0x00, 0x00, 0x7A, 0x6B, 0x01 }[i];
	}
	card.tx.avail[0] = VRING_AVAIL_F_NO_INTERRUPT; /* transmit slots are reclaimed as needed */
	strlcpy(card.ifc.name, "vio0", sizeof(card.ifc.name));
	card.ifc.transmit = vnet_transmit;
	card.ifc.poll = vnet_poll;
	card.ifc.driver = &card;
	irq_install_handler(pci->irq, vnet_irq);
	virtio_ready(io);
	for (int i = 0; i < RX_BUFFERS; i++) {
		post_rx(i);
	}
	netif_register(&card.ifc);
	bool up = !card.status || (inw(io + VIRTIO_CONFIG + 6) & STATUS_LINK_UP);
	kprintf("vio0: virtio network card at pci %02x:%02x.%x, irq %d, "
	        "%02x:%02x:%02x:%02x:%02x:%02x, link %s\n", pci->bus, pci->dev, pci->fn, pci->irq,
	        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], up ? "up" : "down");
}
