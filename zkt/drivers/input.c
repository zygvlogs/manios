/* Input devices for programs that want raw input (the desktop, M12):
 *
 *  /dev/kbd    keys as bytes: ASCII (control characters for Ctrl+letter),
 *              or ZKT_KEY_* (zkt_abi.h) for keys without one; a key
 *              pressed with Alt held comes after a ZKT_KEY_ALT byte. While any
 *              program holds it open, the PS/2 keyboard feeds it instead
 *              of the console; the serial line always feeds the console.
 *  /dev/mouse  one text record per mouse report: "m DX DY BUTTONS\n",
 *              DY growing downwards, BUTTONS bit 0 left, 1 right,
 *              2 middle. Moves are relative: the reader keeps position.
 *
 * Reads block until something arrives and return whole bytes/records;
 * both devices support poll. */
#include "input.h"
#include "cpu.h"
#include "device.h"
#include "kconsole.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "poll.h"
#include "ring.h"
#include "sched.h"
#include "zkt_abi.h"

#define MOUSE_RECORD_MAX 32
#define MOUSE_QUEUE 64

static uint8_t kbd_storage[256];
static struct ring kbd_queue = RING_INIT(kbd_storage);
static struct waitq kbd_ready = WAITQ_INIT;
static volatile unsigned kbd_opens;

struct mouse_event {
	int16_t dx, dy;
	uint8_t buttons;
};
static struct mouse_event mouse_queue[MOUSE_QUEUE];
static unsigned mouse_head, mouse_count;
static struct waitq mouse_ready = WAITQ_INIT;

void input_key(uint8_t key, bool alt)
{
	if (kbd_opens) {
		/* Both bytes or neither: a reader never sees half of Alt+key. */
		if (kbd_queue.size - (kbd_queue.head - kbd_queue.tail) >= (alt ? 2u : 1u)) {
			if (alt) {
				ring_push(&kbd_queue, ZKT_KEY_ALT);
			}
			ring_push(&kbd_queue, key);
		}
		waitq_wake_all(&kbd_ready);
		poll_notify();
	} else if (key < 0x80) {
		console_input((char)key); /* the console has no use for Alt */
	}
}

void input_mouse(int dx, int dy, uint8_t buttons)
{
	uint32_t flags = cpu_irq_save();
	struct mouse_event *last = mouse_count
	    ? &mouse_queue[(mouse_head + mouse_count - 1) % MOUSE_QUEUE] : 0;
	if (last && last->buttons == buttons && mouse_count == MOUSE_QUEUE) {
		/* Full: fold the move into the newest report, keeping the total. */
		last->dx = (int16_t)(last->dx + dx);
		last->dy = (int16_t)(last->dy + dy);
	} else if (mouse_count < MOUSE_QUEUE) {
		mouse_queue[(mouse_head + mouse_count++) % MOUSE_QUEUE] =
		    (struct mouse_event){ (int16_t)dx, (int16_t)dy, buttons };
	}
	cpu_irq_restore(flags);
	waitq_wake_all(&mouse_ready);
	poll_notify();
}

static void kbd_open(struct device *dev)
{
	(void)dev;
	uint32_t flags = cpu_irq_save();
	kbd_opens++;
	cpu_irq_restore(flags);
}

static void kbd_close(struct device *dev)
{
	(void)dev;
	uint32_t flags = cpu_irq_save();
	if (--kbd_opens == 0) {
		while (!ring_empty(&kbd_queue)) {
			ring_pop(&kbd_queue);
		}
	}
	cpu_irq_restore(flags);
}

static long kbd_read(struct device *dev, void *buf, size_t len)
{
	(void)dev;
	uint8_t *out = buf;
	size_t n = 0;
	uint32_t flags = cpu_irq_save();
	while (ring_empty(&kbd_queue)) {
		waitq_sleep(&kbd_ready);
	}
	while (n < len && !ring_empty(&kbd_queue)) {
		out[n++] = ring_pop(&kbd_queue);
	}
	cpu_irq_restore(flags);
	return (long)n;
}

static int kbd_poll(struct device *dev)
{
	(void)dev;
	return !ring_empty(&kbd_queue);
}

static long mouse_read(struct device *dev, void *buf, size_t len)
{
	(void)dev;
	char *out = buf;
	size_t n = 0;
	uint32_t flags = cpu_irq_save();
	while (!mouse_count) {
		waitq_sleep(&mouse_ready);
	}
	while (mouse_count) {
		char record[MOUSE_RECORD_MAX];
		struct mouse_event e = mouse_queue[mouse_head];
		int k = ksnprintf(record, sizeof(record), "m %d %d %d\n", e.dx, e.dy, e.buttons);
		if (n + (size_t)k > len) {
			break;
		}
		memcpy(out + n, record, (size_t)k);
		n += (size_t)k;
		mouse_head = (mouse_head + 1) % MOUSE_QUEUE;
		mouse_count--;
	}
	cpu_irq_restore(flags);
	return n ? (long)n : -EINVAL; /* a buffer too small for one record */
}

static int mouse_poll(struct device *dev)
{
	(void)dev;
	return mouse_count != 0;
}

static const struct char_device_ops kbd_ops = {
	.read = kbd_read, .open = kbd_open, .close = kbd_close, .poll = kbd_poll,
};
static const struct char_device_ops mouse_ops = { .read = mouse_read, .poll = mouse_poll };
static struct device kbd_device = { .name = "kbd", .class = DEVICE_CHAR, .char_ops = &kbd_ops };
static struct device mouse_device = { .name = "mouse", .class = DEVICE_CHAR, .char_ops = &mouse_ops };

void input_register(bool have_mouse)
{
	device_register(&kbd_device);
	if (have_mouse) {
		device_register(&mouse_device);
	}
}
