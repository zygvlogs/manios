/* The PC speaker: the PC/AT's timer channel 2 (an Intel 8254) makes a
 * square wave, and bits 0 and 1 of port 0x61 let it through to the
 * speaker. Device "beep" takes lines of text, "FREQUENCY MILLISECONDS"
 * ("440 250"; a frequency of 0 is a rest), and plays them in turn: a
 * write returns once its notes have played. */
#include "pcspeaker.h"
#include <stdbool.h>
#include <stdint.h>
#include "cpu.h"
#include "device.h"
#include "io.h"
#include "kerrno.h"
#include "mutex.h"
#include "timer.h"

#define PIT_HZ 1193182
#define PIT_CHANNEL2 0x42
#define PIT_COMMAND 0x43
#define PIT_CH2_SQUARE_WAVE 0xB6 /* channel 2, low then high byte, mode 3 */
#define PORT_B 0x61
#define SPEAKER_ON 0x03 /* timer 2's gate, and the speaker's data line */
#define MAX_MS 10000
#define LINE_MAX 32

static struct mutex lock = MUTEX_INIT;

static void tone(uint32_t hz)
{
	uint32_t flags = cpu_irq_save();
	if (hz < 19 || hz > 20000) { /* the divisor's range, and hearing's */
		outb(PORT_B, inb(PORT_B) & ~SPEAKER_ON);
	} else {
		uint32_t divisor = PIT_HZ / hz;
		outb(PIT_COMMAND, PIT_CH2_SQUARE_WAVE);
		outb(PIT_CHANNEL2, (uint8_t)divisor);
		outb(PIT_CHANNEL2, (uint8_t)(divisor >> 8));
		outb(PORT_B, inb(PORT_B) | SPEAKER_ON);
	}
	cpu_irq_restore(flags);
}

/* "FREQ MS": false if the line isn't two numbers. */
static bool parse(const char *line, uint32_t *hz, uint32_t *ms)
{
	uint32_t v[2] = { 0, 0 };
	int n = 0;
	const char *p = line;
	while (n < 2) {
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		if (*p < '0' || *p > '9') {
			return false;
		}
		while (*p >= '0' && *p <= '9') {
			if (v[n] > 1000000) {
				return false;
			}
			v[n] = v[n] * 10 + (uint32_t)(*p++ - '0');
		}
		n++;
	}
	while (*p == ' ' || *p == '\t' || *p == '\r') {
		p++;
	}
	*hz = v[0];
	*ms = v[1] < MAX_MS ? v[1] : MAX_MS;
	return *p == '\0';
}

static long beep_write(struct device *dev, const void *buf, size_t len)
{
	(void)dev;
	const char *in = buf;
	char line[LINE_MAX];
	size_t n = 0;
	mutex_lock(&lock);
	for (size_t i = 0; i <= len; i++) {
		if (i < len && in[i] != '\n') {
			if (n + 1 >= sizeof(line)) {
				tone(0);
				mutex_unlock(&lock);
				return -EINVAL;
			}
			line[n++] = in[i];
			continue;
		}
		line[n] = '\0';
		uint32_t hz, ms;
		if (n && !parse(line, &hz, &ms)) {
			tone(0);
			mutex_unlock(&lock);
			return -EINVAL;
		}
		if (n) {
			tone(hz);
			timer_sleep_ms(ms);
			tone(0);
		}
		n = 0;
	}
	mutex_unlock(&lock);
	return (long)len;
}

static const struct char_device_ops beep_ops = { .write = beep_write };
static struct device beep_device = { .name = "beep", .class = DEVICE_CHAR, .char_ops = &beep_ops };

void pcspeaker_register(void)
{
	tone(0);
	device_register(&beep_device);
}
