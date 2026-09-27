/* The Creative Sound Blaster 16 (and compatibles; QEMU's -device sb16),
 * written from Creative's "Sound Blaster Series Hardware Programming
 * Guide": the digital sound processor (DSP) at 0x220, its mixer, IRQ 5
 * and 16-bit ISA DMA channel 5, as the card comes set.
 *
 * Sound goes out by 16-bit auto-initialised DMA: the 8237 moves the
 * audio layer's ring round and round, and the DSP, told the block size
 * of one fragment, interrupts after each. Reading port 0x22F
 * acknowledges a 16-bit interrupt. */
#include "sb16.h"
#include <stdbool.h>
#include "audio.h"
#include "dma.h"
#include "io.h"
#include "irq.h"
#include "isadma.h"
#include "kprintf.h"
#include "timer.h"

#define BASE 0x220
#define MIXER_ADDR  (BASE + 0x4)
#define MIXER_DATA  (BASE + 0x5)
#define DSP_RESET   (BASE + 0x6)
#define DSP_READ    (BASE + 0xA)
#define DSP_WRITE   (BASE + 0xC) /* bit 7 read: busy */
#define DSP_STATUS  (BASE + 0xE) /* bit 7: data to read; reading acks 8-bit IRQs */
#define DSP_ACK16   (BASE + 0xF)
#define IRQ 5
#define DMA16 5

#define DSP_SET_OUTPUT_RATE 0x41
#define DSP_PLAY16_AUTO     0xB6 /* 16-bit, digital to analogue, auto-init, FIFO */
#define DSP_MODE_STEREO_SIGNED 0x30
#define DSP_PAUSE16         0xD5
#define DSP_SPEAKER_ON      0xD1
#define DSP_EXIT_AUTO16     0xD9
#define DSP_VERSION         0xE1
#define MIXER_IRQ_STATUS    0x82 /* bit 1: a 16-bit DMA interrupt */
#define MIXER_IRQ_SELECT    0x80
#define MIXER_DMA_SELECT    0x81
#define MIXER_MASTER_L      0x30
#define MIXER_MASTER_R      0x31
#define MIXER_VOICE_L       0x32
#define MIXER_VOICE_R       0x33

static uintptr_t ring_phys;

static bool dsp_write(uint8_t v)
{
	for (int i = 0; i < 100000; i++) {
		if (!(inb(DSP_WRITE) & 0x80)) {
			outb(DSP_WRITE, v);
			return true;
		}
	}
	return false;
}

static int dsp_read(void)
{
	for (int i = 0; i < 100000; i++) {
		if (inb(DSP_STATUS) & 0x80) {
			return inb(DSP_READ);
		}
	}
	return -1;
}

static void mixer(uint8_t reg, uint8_t value)
{
	outb(MIXER_ADDR, reg);
	outb(MIXER_DATA, value);
}

static void sb16_irq(void)
{
	outb(MIXER_ADDR, MIXER_IRQ_STATUS);
	if (!(inb(MIXER_DATA) & 0x02)) {
		(void)inb(DSP_STATUS); /* an 8-bit one, not ours: acknowledged anyway */
		return;
	}
	(void)inb(DSP_ACK16);
	audio_fragment_done();
}

static void sb16_start(void)
{
	isa_dma_start(DMA16, ring_phys, AUDIO_BUFFER_BYTES, true, true);
	uint16_t block = AUDIO_FRAGMENT_BYTES / 2 - 1; /* in 16-bit samples, less one */
	dsp_write(DSP_SET_OUTPUT_RATE);
	dsp_write((uint8_t)(AUDIO_RATE >> 8));
	dsp_write((uint8_t)AUDIO_RATE);
	dsp_write(DSP_PLAY16_AUTO);
	dsp_write(DSP_MODE_STEREO_SIGNED);
	dsp_write((uint8_t)block);
	dsp_write((uint8_t)(block >> 8));
}

static void sb16_stop(void)
{
	dsp_write(DSP_PAUSE16);
	dsp_write(DSP_EXIT_AUTO16);
	isa_dma_stop(DMA16);
}

static const struct audio_card card = { "Sound Blaster 16", sb16_start, sb16_stop };

void sb16_init(void)
{
	/* Reset: 1, at least 3 us, 0; then the DSP says 0xAA. */
	outb(DSP_RESET, 1);
	for (int i = 0; i < 16; i++) {
		(void)inb(DSP_STATUS);
	}
	outb(DSP_RESET, 0);
	bool ready = false;
	uint64_t deadline = timer_uptime_ms() + 20;
	while (!ready && timer_uptime_ms() <= deadline) {
		ready = (inb(DSP_STATUS) & 0x80) && inb(DSP_READ) == 0xAA;
	}
	if (!ready || !dsp_write(DSP_VERSION)) {
		return;
	}
	int major = dsp_read(), minor = dsp_read();
	if (major < 4) {
		if (major > 0) {
			kprintf("sb: a Sound Blaster DSP %d.%02d (before the SB16): not supported\n", major,
			        minor);
		}
		return;
	}
	uint8_t *ring = dma_alloc(AUDIO_BUFFER_BYTES, true, &ring_phys);
	if (!ring) {
		kprintf("sb16: no memory for DMA\n");
		return;
	}
	mixer(MIXER_IRQ_SELECT, 0x02);      /* IRQ 5 */
	mixer(MIXER_DMA_SELECT, 0x22);      /* 8-bit DMA 1, 16-bit DMA 5 */
	mixer(MIXER_MASTER_L, 0xF8);        /* loud, not clipping */
	mixer(MIXER_MASTER_R, 0xF8);
	mixer(MIXER_VOICE_L, 0xF8);
	mixer(MIXER_VOICE_R, 0xF8);
	dsp_write(DSP_SPEAKER_ON);
	irq_install_handler(IRQ, sb16_irq);
	if (audio_register(&card, ring) == 0) {
		kprintf("sb16: Sound Blaster 16 (DSP %d.%02d) at 0x%x, irq %d, dma %d: /dev/audio\n",
		        major, minor, BASE, IRQ, DMA16);
	}
}
