/* PS/2 mouse on the 8042 controller's auxiliary port, IRQ 12: standard
 * 3-byte reports (buttons and signs, then X and Y movement), as in the
 * IBM PS/2 technical reference. Reports go to input_mouse(). */
#include "ps2mouse.h"
#include "cpu.h"
#include "input.h"
#include "io.h"
#include "irq.h"

#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_COMMAND 0x64
#define STATUS_OUTPUT_FULL 0x01
#define STATUS_INPUT_FULL  0x02
#define STATUS_AUX_DATA    0x20

#define CMD_READ_CONFIG  0x20
#define CMD_WRITE_CONFIG 0x60
#define CMD_ENABLE_AUX   0xA8
#define CMD_WRITE_AUX    0xD4
#define CONFIG_AUX_IRQ       0x02
#define CONFIG_AUX_CLOCK_OFF 0x20

#define MOUSE_SET_DEFAULTS 0xF6
#define MOUSE_ENABLE       0xF4
#define MOUSE_ACK          0xFA
#define MOUSE_IRQ 12
#define SPIN_LIMIT 100000

#define FIRST_BYTE_SYNC 0x08 /* always set in a report's first byte */
#define OVERFLOW 0xC0

static uint8_t report[3];
static int have;

static bool wait_status(uint8_t mask, bool set)
{
	for (int i = 0; i < SPIN_LIMIT; i++) {
		if (!!(inb(PS2_STATUS) & mask) == set) {
			return true;
		}
	}
	return false;
}

static void command(uint8_t cmd)
{
	wait_status(STATUS_INPUT_FULL, false);
	outb(PS2_COMMAND, cmd);
}

static bool mouse_command(uint8_t cmd)
{
	command(CMD_WRITE_AUX);
	wait_status(STATUS_INPUT_FULL, false);
	outb(PS2_DATA, cmd);
	return wait_status(STATUS_OUTPUT_FULL, true) && inb(PS2_DATA) == MOUSE_ACK;
}

static void mouse_irq(void)
{
	uint8_t status = inb(PS2_STATUS);
	if (!(status & STATUS_OUTPUT_FULL) || !(status & STATUS_AUX_DATA)) {
		return;
	}
	uint8_t b = inb(PS2_DATA);
	if (have == 0 && !(b & FIRST_BYTE_SYNC)) {
		return; /* out of step: wait for a first byte */
	}
	report[have++] = b;
	if (have < 3) {
		return;
	}
	have = 0;
	if (report[0] & OVERFLOW) {
		return;
	}
	/* 9-bit two's complement: the sign bits are in the first byte. */
	int dx = report[1] - ((report[0] << 4) & 0x100);
	int dy = report[2] - ((report[0] << 3) & 0x100);
	input_mouse(dx, -dy, report[0] & 0x07); /* the mouse counts Y upwards */
}

bool ps2mouse_init(void)
{
	uint32_t flags = cpu_irq_save();
	command(CMD_ENABLE_AUX);
	command(CMD_READ_CONFIG);
	bool ok = wait_status(STATUS_OUTPUT_FULL, true);
	if (ok) {
		uint8_t config = inb(PS2_DATA);
		config |= CONFIG_AUX_IRQ;
		config &= (uint8_t)~CONFIG_AUX_CLOCK_OFF;
		command(CMD_WRITE_CONFIG);
		wait_status(STATUS_INPUT_FULL, false);
		outb(PS2_DATA, config);
		ok = mouse_command(MOUSE_SET_DEFAULTS) && mouse_command(MOUSE_ENABLE);
	}
	cpu_irq_restore(flags);
	if (ok) {
		irq_install_handler(MOUSE_IRQ, mouse_irq);
	}
	return ok;
}
