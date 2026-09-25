#include "ps2kbd.h"
#include <stdbool.h>
#include <stdint.h>
#include "io.h"
#include "irq.h"
#include "input.h"
#include "zkt_abi.h"

#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_COMMAND 0x64

#define STATUS_OUTPUT_FULL 0x01
#define STATUS_INPUT_FULL  0x02
#define STATUS_AUX_DATA    0x20

#define CMD_READ_CONFIG  0x20
#define CMD_WRITE_CONFIG 0x60
#define CMD_ENABLE_PORT1 0xAE

#define CONFIG_PORT1_IRQ       0x01
#define CONFIG_PORT1_CLOCK_OFF 0x10
#define CONFIG_TRANSLATE       0x40 /* controller converts to scancode set 1 */

#define KBD_IRQ 1
#define SPIN_LIMIT 100000 /* so a machine without a PS/2 controller can't hang boot */

#define SC_RELEASE    0x80
#define SC_EXTENDED   0xE0
#define SC_LCTRL      0x1D
#define SC_LSHIFT     0x2A
#define SC_RSHIFT     0x36
#define SC_CAPSLOCK   0x3A
#define SC_ENTER      0x1C
#define SC_F1         0x3B
#define SC_F10        0x44

/* Extended (0xE0-prefixed) keys that have a ZKT_KEY_ code. */
static const struct {
	uint8_t code, key;
} EXTENDED_KEYS[] = {
	{ 0x48, ZKT_KEY_UP },   { 0x50, ZKT_KEY_DOWN }, { 0x4B, ZKT_KEY_LEFT },  { 0x4D, ZKT_KEY_RIGHT },
	{ 0x47, ZKT_KEY_HOME }, { 0x4F, ZKT_KEY_END },  { 0x49, ZKT_KEY_PGUP },  { 0x51, ZKT_KEY_PGDN },
	{ 0x52, ZKT_KEY_INSERT }, { 0x53, ZKT_KEY_DELETE },
};

/* Scancode set 1, US layout, codes 0x00-0x39; 0 = no character. Split
 * into one literal per row of the keyboard. */
static const char NORMAL[0x3A] =
	"\0\033" "1234567890-=" "\b\t"
	"qwertyuiop[]" "\n\0"
	"asdfghjkl;'`" "\0\\"
	"zxcvbnm,./" "\0*\0 ";
static const char SHIFTED[0x3A] =
	"\0\033" "!@#$%^&*()_+" "\b\t"
	"QWERTYUIOP{}" "\n\0"
	"ASDFGHJKL:\"~" "\0|"
	"ZXCVBNM<>?" "\0*\0 ";

static bool shift, ctrl, caps_lock, extended;

static bool wait_status(uint8_t mask, bool set)
{
	for (int i = 0; i < SPIN_LIMIT; i++) {
		if (!!(inb(PS2_STATUS) & mask) == set) {
			return true;
		}
	}
	return false;
}

static void send_command(uint8_t cmd)
{
	wait_status(STATUS_INPUT_FULL, false);
	outb(PS2_COMMAND, cmd);
}

static void kbd_irq(void)
{
	if (inb(PS2_STATUS) & STATUS_AUX_DATA) {
		return; /* the mouse's byte: its IRQ handler takes it */
	}
	uint8_t code = inb(PS2_DATA);
	if (code == SC_EXTENDED) {
		extended = true;
		return;
	}

	bool released = code & SC_RELEASE;
	code &= (uint8_t)~SC_RELEASE;
	bool was_extended = extended;
	extended = false;

	if (code == SC_LCTRL) { /* right Ctrl is the same code, extended */
		ctrl = !released;
		return;
	}
	if (was_extended) {
		if (released) {
			return;
		}
		if (code == SC_ENTER) { /* keypad Enter */
			input_key('\n');
		}
		for (unsigned i = 0; i < sizeof(EXTENDED_KEYS) / sizeof(EXTENDED_KEYS[0]); i++) {
			if (EXTENDED_KEYS[i].code == code) {
				input_key(EXTENDED_KEYS[i].key);
			}
		}
		return;
	}
	if (code == SC_LSHIFT || code == SC_RSHIFT) {
		shift = !released;
		return;
	}
	if (!released && code >= SC_F1 && code <= SC_F10) {
		input_key((uint8_t)(ZKT_KEY_F1 + code - SC_F1));
		return;
	}
	if (released || code >= sizeof(NORMAL)) {
		return;
	}
	if (code == SC_CAPSLOCK) {
		caps_lock = !caps_lock;
		return;
	}

	char c = shift ? SHIFTED[code] : NORMAL[code];
	if (caps_lock && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
		c ^= 0x20;
	}
	if (ctrl && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
		c &= 0x1F;
	}
	if (c) {
		input_key((uint8_t)c);
	}
}

void ps2kbd_init(void)
{
	/* Drop anything left over from the BIOS or the boot loader. */
	for (int i = 0; i < 16 && (inb(PS2_STATUS) & STATUS_OUTPUT_FULL); i++) {
		inb(PS2_DATA);
	}

	send_command(CMD_READ_CONFIG);
	if (!wait_status(STATUS_OUTPUT_FULL, true)) {
		return; /* no controller answered */
	}
	uint8_t config = inb(PS2_DATA);
	config |= CONFIG_PORT1_IRQ | CONFIG_TRANSLATE;
	config &= (uint8_t)~CONFIG_PORT1_CLOCK_OFF;
	send_command(CMD_WRITE_CONFIG);
	wait_status(STATUS_INPUT_FULL, false);
	outb(PS2_DATA, config);
	send_command(CMD_ENABLE_PORT1);

	irq_install_handler(KBD_IRQ, kbd_irq);
}
