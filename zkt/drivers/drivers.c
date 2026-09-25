#include "drivers.h"
#include "kconsole.h"
#include "ps2kbd.h"
#include "serial.h"
#include "vga_text.h"

void drivers_init(void)
{
	console_register();
	serial_start();
	vga_register();
	ps2kbd_init();
}
