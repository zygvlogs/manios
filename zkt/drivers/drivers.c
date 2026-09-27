#include "drivers.h"
#include "ac97.h"
#include "acpi.h"
#include "adlib.h"
#include "ahci.h"
#include "ata.h"
#include "fb.h"
#include "floppy.h"
#include "input.h"
#include "kconsole.h"
#include "lpt.h"
#include "null.h"
#include "nvram.h"
#include "pcspeaker.h"
#include "sysname.h"
#include "sysstat.h"
#include "pci.h"
#include "ps2kbd.h"
#include "ps2mouse.h"
#include "rtc.h"
#include "sb16.h"
#include "serial.h"
#include "uart.h"
#include "vga_text.h"
#include "virtio_blk.h"

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
	floppy_init();
	pci_init();
	ahci_init();
	virtio_blk_init();
	pcspeaker_register();
	sb16_init();
	ac97_init();
	adlib_init();
	uart_init();
	lpt_init();
	nvram_register();
	acpi_init();
	fb_init();
	rtc_init();
}
