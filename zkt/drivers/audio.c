/* The audio layer (audio.h). Positions round the ring are counted, not
 * wrapped: `started` is how many fragments the card has begun (the one
 * playing is started - 1, modulo the ring), `filled` how many the writer
 * has completed. The writer fills fragment `filled` while it is ahead of
 * the card -- filled >= started -- and less than a ring ahead. A played
 * fragment is silenced, so that one not refilled in time plays silence;
 * should the card catch the writer up (an underrun), the writer's
 * partial fragment moves on to the next one the card will play. */
#include "audio.h"
#include <stdbool.h>
#include "cpu.h"
#include "device.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "mutex.h"
#include "sched.h"
#include "timer.h"

#define B AUDIO_FRAGMENT_BYTES
#define N AUDIO_FRAGMENTS
#define FRAGMENT_TIMEOUT_TICKS (TIMER_HZ / 2) /* a fragment is 23 ms */

static const struct audio_card *card;
static uint8_t *ring;
static struct mutex lock = MUTEX_INIT; /* writers, and closing */
static struct waitq progress = WAITQ_INIT;
static volatile uint32_t started;
static uint32_t filled, offset; /* offset: bytes in fragment `filled` */
static bool running;
static uint32_t opens, underruns;

static uint8_t *fragment(uint32_t n)
{
	return ring + (n % N) * B;
}

void audio_fragment_done(void)
{
	if (!running) {
		return;
	}
	/* Fragment started - 1 has played; the card is on the next. */
	memset(fragment(started - 1), 0, B);
	started++;
	if (filled < started - 1) {
		underruns++;
	}
	waitq_wake_all(&progress);
}

/* Waits (interrupts off) for the card to move on; false if it hasn't
 * for far too long -- a card that stopped interrupting. */
static bool wait_card(void)
{
	return waitq_sleep_until(&progress, timer_ticks() + FRAGMENT_TIMEOUT_TICKS);
}

static void stop(void)
{
	card->stop();
	running = false;
	started = filled = offset = 0;
	memset(ring, 0, AUDIO_BUFFER_BYTES);
}

static long audio_write(struct device *dev, const void *buf, size_t len)
{
	(void)dev;
	const uint8_t *in = buf;
	size_t done = 0;
	mutex_lock(&lock);
	while (done < len) {
		uint32_t flags = cpu_irq_save();
		/* Room: at most a ring ahead of the fragment playing. */
		while (running && filled >= started - 1 + N) {
			if (!wait_card()) {
				cpu_irq_restore(flags);
				stop();
				mutex_unlock(&lock);
				return done ? (long)done : -EIO;
			}
		}
		if (running && filled < started) {
			/* The card caught up: carry what there is to the fragment it plays next. */
			if (offset && (filled % N) != (started % N)) {
				memcpy(fragment(started), fragment(filled), offset);
			}
			filled = started;
		}
		cpu_irq_restore(flags);
		size_t chunk = B - offset < len - done ? B - offset : len - done;
		memcpy(fragment(filled) + offset, in + done, chunk);
		done += chunk;
		offset += (uint32_t)chunk;
		if (offset == B) {
			flags = cpu_irq_save();
			filled++;
			offset = 0;
			if (!running) {
				running = true;
				started = 1; /* fragment 0 */
				card->start();
			}
			cpu_irq_restore(flags);
		}
	}
	mutex_unlock(&lock);
	return (long)len;
}

static void audio_open(struct device *dev)
{
	(void)dev;
	uint32_t flags = cpu_irq_save();
	opens++;
	cpu_irq_restore(flags);
}

/* The last close: what is left plays out, then the card stops. */
static void audio_close(struct device *dev)
{
	(void)dev;
	mutex_lock(&lock);
	uint32_t flags = cpu_irq_save();
	bool last = --opens == 0;
	cpu_irq_restore(flags);
	if (last && (running || offset)) {
		if (offset) {
			memset(fragment(filled) + offset, 0, B - offset); /* silence to the end */
			flags = cpu_irq_save();
			filled++;
			offset = 0;
			if (!running) {
				running = true;
				started = 1;
				card->start();
			}
			cpu_irq_restore(flags);
		}
		flags = cpu_irq_save();
		/* Played out once the card has begun the fragment after the last. */
		while (running && started <= filled && wait_card()) {
		}
		cpu_irq_restore(flags);
		stop();
	}
	mutex_unlock(&lock);
}

/* Reading gives the format, the card and the underruns so far. */
static long audio_read(struct device *dev, uint32_t off, void *buf, size_t len)
{
	(void)dev;
	char text[96];
	int n = ksnprintf(text, sizeof(text), "%s: %u Hz, 16-bit signed, stereo; %lu underruns\n",
	                  card->name, AUDIO_RATE, (unsigned long)underruns);
	if (off >= (uint32_t)n) {
		return 0;
	}
	if (len > (size_t)n - off) {
		len = (size_t)n - off;
	}
	memcpy(buf, text + off, len);
	return (long)len;
}

static const struct char_device_ops audio_ops = {
	.write = audio_write, .pread = audio_read, .open = audio_open, .close = audio_close,
};
static struct device audio_device = { .name = "audio", .class = DEVICE_CHAR, .char_ops = &audio_ops };

int audio_register(const struct audio_card *c, uint8_t *r)
{
	if (card) {
		return -EBUSY;
	}
	card = c;
	ring = r;
	return device_register(&audio_device);
}
