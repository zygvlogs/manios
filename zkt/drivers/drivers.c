#include "drivers.h"
#include "ata.h"
#include "fb.h"
#include "input.h"
#include "kconsole.h"
#include "null.h"
#include "sysname.h"
#include "sysstat.h"
#include "pci.h"
#include "ps2kbd.h"
#include "ps2mouse.h"
#include "rtc.h"
#include "serial.h"
#include "vga_text.h"

void drivers_init(void)
{
	console_register();
	serial_start();
	vga_register();
	null_register();
	sysname_register();
	sysstat_register();
	ps2kbd_init();
	input_register(ps2mouse_init());
	ata_init();
	pci_init();
	fb_init();
	rtc_init();
}
