/* AC'97 audio on Intel's ICH controllers (the 82801AA and the family
 * after it, and compatibles; QEMU's -device AC97, VirtualBox's default
 * sound card), written from Intel's "AC '97 Audio Controller
 * Programmer's Reference Manual" and the AC'97 2.3 codec specification.
 *
 * Two I/O BARs: the codec's mixer registers (NAM, BAR 0) and the bus
 * master (NABM, BAR 1), whose PCM-out channel plays a list of 32 buffer
 * descriptors, each an address and a length in samples. Here descriptor
 * k points at fragment k mod 4 of the audio layer's ring, and each asks
 * for an interrupt when done; the last valid index is kept one behind
 * the current, so the channel goes round and round. The codec plays
 * 44,100 Hz if it has variable rates (VRA); else it stays at its 48,000. */
#include "ac97.h"
#include <stdbool.h>
#include "audio.h"
#include "dma.h"
#include "io.h"
#include "irq.h"
#include "kprintf.h"
#include "pci.h"
#include "timer.h"

/* The codec (NAM). */
#define NAM_RESET        0x00
#define NAM_MASTER       0x02
#define NAM_PCM_OUT      0x18
#define NAM_EXT_ID       0x28
#define NAM_EXT_CTRL     0x2A
#define NAM_FRONT_RATE   0x2C
#define EXT_VRA          0x0001

/* The bus master (NABM): PCM out's registers, then the global ones. */
#define PO_BDBAR 0x10
#define PO_CIV   0x14
#define PO_LVI   0x15
#define PO_SR    0x16
#define PO_CR    0x1B
#define GLOB_CNT 0x2C
#define GLOB_STA 0x30
#define SR_DCH   0x01
#define SR_LVBCI 0x04
#define SR_BCIS  0x08 /* a buffer completed */
#define SR_FIFOE 0x10
#define CR_RPBM  0x01 /* run */
#define CR_RR    0x02 /* reset the channel's registers */
#define CR_LVBIE 0x04
#define CR_FEIE  0x08
#define CR_IOCE  0x10
#define CNT_COLD 0x02 /* out of cold reset */
#define STA_CODEC_READY 0x100

#define DESCRIPTORS 32
#define BD_IOC (1u << 31)

struct buffer_descriptor {
	uint32_t address;
	uint32_t samples_flags; /* samples (16-bit) in bits 0-15, flags above */
};

static uint16_t nam, nabm;
static struct buffer_descriptor *list;
static uintptr_t list_phys, ring_phys;

static void ac97_irq(void)
{
	uint16_t sr = inw(nabm + PO_SR);
	if (!(sr & (SR_BCIS | SR_LVBCI | SR_FIFOE))) {
		return; /* not ours */
	}
	outw(nabm + PO_SR, sr & (SR_BCIS | SR_LVBCI | SR_FIFOE)); /* write 1s to clear */
	if (sr & SR_BCIS) {
		/* Keep the last valid one just behind the current: never the end. */
		outb(nabm + PO_LVI, (uint8_t)((inb(nabm + PO_CIV) + DESCRIPTORS - 1) % DESCRIPTORS));
		audio_fragment_done();
	}
}

static void reset_channel(void)
{
	outb(nabm + PO_CR, 0);
	outb(nabm + PO_CR, CR_RR);
	for (int i = 0; i < 1000 && (inb(nabm + PO_CR) & CR_RR); i++) {
	}
}

static void ac97_start(void)
{
	reset_channel();
	outl(nabm + PO_BDBAR, (uint32_t)list_phys);
	outb(nabm + PO_LVI, DESCRIPTORS - 1);
	outb(nabm + PO_CR, CR_RPBM | CR_IOCE | CR_FEIE);
}

static void ac97_stop(void)
{
	reset_channel();
}

static const struct audio_card card = { "AC'97", ac97_start, ac97_stop };

void ac97_init(void)
{
	/* The ICH's AC'97 functions: 82801AA, AB, BA, ICH3 to ICH7. */
	static const uint16_t ICH[] = { 0x2415, 0x2425, 0x2445, 0x2485, 0x24C5, 0x24D5, 0x266E, 0x27DE };
	const struct pci_device *pci = 0;
	for (size_t i = 0; i < sizeof(ICH) / sizeof(ICH[0]) && !pci; i++) {
		pci = pci_find(0x8086, ICH[i]);
	}
	if (!pci) {
		return;
	}
	nam = (uint16_t)pci_bar_io(pci, 0);
	nabm = (uint16_t)pci_bar_io(pci, 1);
	if (!nam || !nabm || pci->irq == 0 || pci->irq >= 16) {
		return;
	}
	pci_enable(pci);
	outl(nabm + GLOB_CNT, CNT_COLD);
	uint64_t deadline = timer_uptime_ms() + 600;
	while (!(inl(nabm + GLOB_STA) & STA_CODEC_READY) && timer_uptime_ms() < deadline) {
		timer_sleep_ms(1);
	}
	if (!(inl(nabm + GLOB_STA) & STA_CODEC_READY)) {
		kprintf("ac97: the codec isn't ready\n");
		return;
	}
	outw(nam + NAM_RESET, 1);
	outw(nam + NAM_MASTER, 0x0000);  /* 0 dB, not muted */
	outw(nam + NAM_PCM_OUT, 0x0808); /* 0 dB */
	unsigned rate = 48000;
	if (inw(nam + NAM_EXT_ID) & EXT_VRA) {
		outw(nam + NAM_EXT_CTRL, (uint16_t)(inw(nam + NAM_EXT_CTRL) | EXT_VRA));
		outw(nam + NAM_FRONT_RATE, AUDIO_RATE);
		rate = inw(nam + NAM_FRONT_RATE);
	}

	uint8_t *ring = dma_alloc(AUDIO_BUFFER_BYTES, false, &ring_phys);
	list = dma_alloc(DESCRIPTORS * sizeof(struct buffer_descriptor), false, &list_phys);
	if (!ring || !list) {
		kprintf("ac97: no memory for DMA\n");
		return;
	}
	for (int k = 0; k < DESCRIPTORS; k++) {
		list[k].address = (uint32_t)(ring_phys + (uintptr_t)(k % AUDIO_FRAGMENTS) * AUDIO_FRAGMENT_BYTES);
		list[k].samples_flags = BD_IOC | (AUDIO_FRAGMENT_BYTES / 2);
	}
	reset_channel();
	irq_install_handler(pci->irq, ac97_irq);
	if (audio_register(&card, ring) == 0) {
		kprintf("ac97: AC'97 (%04x:%04x) at pci %02x:%02x.%x, irq %d, %u Hz: /dev/audio\n",
		        pci->vendor, pci->device, pci->bus, pci->dev, pci->fn, pci->irq, rate);
	} else {
		kprintf("ac97: AC'97 found, but another card has /dev/audio\n");
	}
}
