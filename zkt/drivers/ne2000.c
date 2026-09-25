/* NE2000-compatible ISA Ethernet (National Semiconductor DP8390 core),
 * the classic card of the 386 era and what QEMU's ne2k_isa emulates.
 * Written from the DP8390D datasheet's register description; programmed
 * I/O through the card's "remote DMA" in 16-bit mode.
 *
 * The card's 16 KiB of buffer memory spans pages 0x40-0x7F (256 bytes
 * each): a 6-page transmit buffer, then the receive ring. The IRQ
 * handler only acknowledges and records events; receiving and sending
 * run on threads, serialised by a mutex because both use the remote
 * DMA channel. */
#include "ne2000.h"
#include <stdbool.h>
#include "cpu.h"
#include "io.h"
#include "irq.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "mutex.h"
#include "net.h"
#include "sched.h"
#include "timer.h"

#define NE_BASE 0x300
#define NE_IRQ 9

/* Registers (page 0 unless noted). */
#define CR     0x00
#define PSTART 0x01
#define PSTOP  0x02
#define BNRY   0x03
#define TPSR   0x04
#define TBCR0  0x05
#define TBCR1  0x06
#define ISR    0x07
#define RSAR0  0x08
#define RSAR1  0x09
#define RBCR0  0x0A
#define RBCR1  0x0B
#define RCR    0x0C
#define TCR    0x0D
#define DCR    0x0E
#define IMR    0x0F
#define PAR0   0x01 /* page 1 */
#define CURR   0x07 /* page 1 */
#define MAR0   0x08 /* page 1 */
#define DATA   0x10
#define RESET  0x1F

/* CR bits. */
#define CR_STOP   0x01
#define CR_START  0x02
#define CR_TXP    0x04
#define CR_RREAD  0x08
#define CR_RWRITE 0x10
#define CR_NODMA  0x20
#define CR_PAGE1  0x40

/* ISR bits. */
#define ISR_PRX 0x01 /* packet received */
#define ISR_PTX 0x02 /* packet sent */
#define ISR_RXE 0x04
#define ISR_TXE 0x08
#define ISR_OVW 0x10 /* receive ring overflow */
#define ISR_RDC 0x40 /* remote DMA complete */
#define ISR_RST 0x80

#define TX_PAGE 0x40
#define RX_FIRST 0x46
#define RX_LAST 0x80 /* one past the ring */

#define SPIN_LIMIT 100000
#define TX_TIMEOUT_TICKS (100 * TIMER_HZ / 1000)

static struct netif ne0;
static struct mutex dma_lock = MUTEX_INIT;
static struct waitq tx_done = WAITQ_INIT;
static volatile bool tx_busy;
static volatile uint8_t pending_isr;
static uint8_t frame_buf[ETH_FRAME_MAX + 4];

static void reg_write(uint8_t reg, uint8_t v) { outb(NE_BASE + reg, v); }
static uint8_t reg_read(uint8_t reg) { return inb(NE_BASE + reg); }

static bool wait_isr(uint8_t bit)
{
	for (int i = 0; i < SPIN_LIMIT; i++) {
		if (reg_read(ISR) & bit) {
			return true;
		}
	}
	return false;
}

/* Remote DMA, 16-bit: `len` is rounded up to even. */
static void dma_setup(uint16_t addr, uint16_t len, uint8_t command)
{
	reg_write(RBCR0, (uint8_t)len);
	reg_write(RBCR1, (uint8_t)(len >> 8));
	reg_write(RSAR0, (uint8_t)addr);
	reg_write(RSAR1, (uint8_t)(addr >> 8));
	reg_write(CR, command | CR_START);
}

static void dma_finish(void)
{
	wait_isr(ISR_RDC);
	reg_write(ISR, ISR_RDC);
}

static void dma_read(uint16_t addr, uint8_t *buf, uint16_t len)
{
	uint16_t words = (uint16_t)((len + 1) / 2);
	dma_setup(addr, (uint16_t)(words * 2), CR_RREAD);
	for (uint16_t i = 0; i < words; i++) {
		uint16_t w = inw(NE_BASE + DATA);
		buf[2 * i] = (uint8_t)w;
		if (2 * i + 1 < len) {
			buf[2 * i + 1] = (uint8_t)(w >> 8);
		}
	}
	dma_finish();
}

static void dma_write(uint16_t addr, const uint8_t *buf, uint16_t len)
{
	uint16_t words = (uint16_t)((len + 1) / 2);
	dma_setup(addr, (uint16_t)(words * 2), CR_RWRITE);
	for (uint16_t i = 0; i < words; i++) {
		uint16_t hi = 2 * i + 1 < len ? buf[2 * i + 1] : 0;
		outw(NE_BASE + DATA, (uint16_t)(buf[2 * i] | hi << 8));
	}
	dma_finish();
}

#define IMR_BITS (ISR_PRX | ISR_PTX | ISR_RXE | ISR_TXE | ISR_OVW)

/* The card holds its interrupt line up while any enabled ISR bit is set,
 * but the 8259 only notices a rising edge: acknowledge until none is
 * left, or an event arriving mid-handler would keep the line up and no
 * interrupt would ever come again. */
static void ne_irq(void)
{
	uint8_t isr;
	while ((isr = reg_read(ISR) & IMR_BITS) != 0) {
		reg_write(ISR, isr);
		pending_isr |= isr;
		if (isr & (ISR_PTX | ISR_TXE)) {
			tx_busy = false;
			waitq_wake_all(&tx_done);
		}
		if (isr & (ISR_PRX | ISR_RXE | ISR_OVW)) {
			netif_notify();
		}
	}
}

static uint8_t read_curr(void)
{
	/* CURR is on page 1, where ISR's offset holds something else: keep
	 * the IRQ handler out while the page is switched. */
	uint32_t flags = cpu_irq_save();
	reg_write(CR, CR_PAGE1 | CR_NODMA | CR_START);
	uint8_t curr = reg_read(CURR);
	reg_write(CR, CR_NODMA | CR_START);
	cpu_irq_restore(flags);
	return curr;
}

/* Drains the receive ring. Each packet starts on a page with a 4-byte
 * header: status, next page, length (header included). */
static void ne_poll(struct netif *ifc)
{
	mutex_lock(&dma_lock);
	for (;;) {
		uint8_t next = reg_read(BNRY) + 1;
		if (next >= RX_LAST) {
			next = RX_FIRST;
		}
		if (next == read_curr()) {
			break;
		}
		uint8_t hdr[4];
		dma_read((uint16_t)(next << 8), hdr, 4);
		uint16_t len = (uint16_t)(hdr[2] | hdr[3] << 8);
		if (hdr[1] < RX_FIRST || hdr[1] >= RX_LAST || len < 4 + ETH_HEADER || len > sizeof(frame_buf)) {
			/* A corrupt ring: drop everything in it. */
			uint8_t curr = read_curr();
			reg_write(BNRY, curr == RX_FIRST ? RX_LAST - 1 : curr - 1);
			ifc->rx_dropped++;
			break;
		}
		/* The remote DMA wraps around the ring by itself. */
		dma_read((uint16_t)((next << 8) + 4), frame_buf, (uint16_t)(len - 4));
		reg_write(BNRY, hdr[1] == RX_FIRST ? RX_LAST - 1 : hdr[1] - 1);
		mutex_unlock(&dma_lock);
		netif_input(ifc, frame_buf, len - 4u);
		mutex_lock(&dma_lock);
	}
	/* The ring has room again; the card resumes by itself (QEMU), and
	 * the pending overflow is acknowledged. */
	pending_isr = 0;
	mutex_unlock(&dma_lock);
}

static int ne_transmit(struct netif *ifc, const uint8_t *frame, size_t len)
{
	(void)ifc;
	if (len > ETH_FRAME_MAX) {
		return -EINVAL;
	}
	mutex_lock(&dma_lock);
	uint64_t deadline = timer_ticks() + TX_TIMEOUT_TICKS;
	uint32_t flags = cpu_irq_save();
	while (tx_busy && waitq_sleep_until(&tx_done, deadline)) {
	}
	tx_busy = true; /* a lost completion interrupt only costs the timeout */
	cpu_irq_restore(flags);

	/* Short frames are padded here, in one transfer: a second remote DMA
	 * starting at an odd address would, moving whole words, overwrite
	 * the frame's last byte. */
	uint8_t padded[ETH_FRAME_MIN];
	uint16_t n = (uint16_t)len;
	if (n < ETH_FRAME_MIN) {
		memcpy(padded, frame, n);
		memset(padded + n, 0, ETH_FRAME_MIN - n);
		frame = padded;
		n = ETH_FRAME_MIN;
	}
	dma_write(TX_PAGE << 8, frame, n);
	reg_write(TPSR, TX_PAGE);
	reg_write(TBCR0, (uint8_t)n);
	reg_write(TBCR1, (uint8_t)(n >> 8));
	reg_write(CR, CR_NODMA | CR_TXP | CR_START);
	mutex_unlock(&dma_lock);
	return 0;
}

void ne2000_probe(void)
{
	/* No card: the bus floats high. */
	if (reg_read(CR) == 0xFF) {
		return;
	}
	reg_write(RESET, reg_read(RESET));
	if (!wait_isr(ISR_RST)) {
		return;
	}
	reg_write(ISR, 0xFF);

	reg_write(CR, CR_NODMA | CR_STOP);
	reg_write(DCR, 0x49); /* 16-bit transfers, normal mode, FIFO threshold 8 */
	reg_write(RBCR0, 0);
	reg_write(RBCR1, 0);
	reg_write(IMR, 0);
	reg_write(ISR, 0xFF);
	reg_write(RCR, 0x20); /* monitor mode while configuring */
	reg_write(TCR, 0x02); /* internal loopback */

	/* The station address PROM: in 16-bit mode each byte comes as a word. */
	uint8_t prom[32];
	dma_read(0, prom, sizeof(prom));
	for (int i = 0; i < ETH_ALEN; i++) {
		ne0.mac[i] = prom[2 * i];
	}

	reg_write(PSTART, RX_FIRST);
	reg_write(PSTOP, RX_LAST);
	reg_write(BNRY, RX_FIRST);
	reg_write(TPSR, TX_PAGE);
	reg_write(CR, CR_PAGE1 | CR_NODMA | CR_STOP);
	for (int i = 0; i < ETH_ALEN; i++) {
		reg_write(PAR0 + i, ne0.mac[i]);
	}
	for (int i = 0; i < 8; i++) {
		reg_write(MAR0 + i, 0); /* no multicast */
	}
	reg_write(CURR, RX_FIRST + 1);
	reg_write(CR, CR_NODMA | CR_STOP);
	reg_write(RCR, 0x04); /* our address and broadcast */
	reg_write(TCR, 0x00);
	reg_write(ISR, 0xFF);
	reg_write(IMR, IMR_BITS);
	reg_write(CR, CR_NODMA | CR_START);

	strlcpy(ne0.name, "ne0", sizeof(ne0.name));
	ne0.transmit = ne_transmit;
	ne0.poll = ne_poll;
	irq_install_handler(NE_IRQ, ne_irq);
	netif_register(&ne0);
	kprintf("ne0: NE2000 at 0x%x irq %d, %02x:%02x:%02x:%02x:%02x:%02x\n", NE_BASE, NE_IRQ,
	        ne0.mac[0], ne0.mac[1], ne0.mac[2], ne0.mac[3], ne0.mac[4], ne0.mac[5]);
}
