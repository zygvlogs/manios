/* The framebuffer: devices "fb" and "fbctl" (M11). Design notes:
 * docs/milestones/M11-graphics.md.
 *
 * Two kinds of graphics mode:
 *  - Bochs VBE ("BGA"): any size, 32-bit pixels (0x00RRGGBB), a linear
 *    framebuffer in a PCI BAR. QEMU's and Bochs's standard adapter have
 *    it, and so do the adapters that grew out of it or kept it for their
 *    BIOS: VirtualBox's (VBoxVGA, VBoxSVGA) and VMware's SVGA II (which
 *    is also VirtualBox's VMSVGA and QEMU's -vga vmware);
 *  - VGA mode 13h, on any VGA card: 320x200, one RGB 3-3-2 byte per pixel.
 * Text mode's state is saved on the way into graphics and restored on
 * the way out (vga_hw.c, vga_text.c).
 *
 * fbctl reads as "text\n", or "WIDTH HEIGHT DEPTH PITCH FORMAT DRIVER\n";
 * writing "mode W H", "mode vga" or "text" changes the mode. fb is the
 * pixels, read and written at byte offsets (row y starts at y * PITCH). */
#include "fb.h"
#include <stdbool.h>
#include "device.h"
#include "io.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "memlayout.h"
#include "mutex.h"
#include "pci.h"
#include "sched.h"
#include "timer.h"
#include "vga_hw.h"
#include "vga_text.h"
#include "vmm.h"

#define VBE_INDEX 0x01CE
#define VBE_DATA  0x01CF
#define VBE_ID     0
#define VBE_XRES   1
#define VBE_YRES   2
#define VBE_BPP    3
#define VBE_ENABLE 4
#define VBE_BANK   5
#define VBE_VIRT_WIDTH 6
#define VBE_VIRT_HEIGHT 7
#define VBE_X_OFFSET 8
#define VBE_Y_OFFSET 9
#define VBE_MEMORY_64K 10
#define VBE_ENABLED 0x01
#define VBE_LFB     0x40

#define PCI_COMMAND 0x04
#define PCI_COMMAND_MEMORY 0x2
#define PCI_BAR0 0x10
#define PCI_BAR_IO 0x1
#define PCI_BAR_64BIT 0x4
#define PCI_BAR_PREFETCH 0x8

/* Display adapters with the Bochs VBE registers (VBE_INDEX/VBE_DATA). */
static const struct {
	uint16_t vendor, device;
	const char *name;
} ADAPTERS[] = {
	{ 0x1234, 0x1111, "Bochs VBE" },
	{ 0x80EE, 0xBEEF, "VirtualBox VGA" },
	{ 0x15AD, 0x0405, "VMware SVGA II" },
};

enum kind { TEXT, BGA, VGA13 };

static struct mutex lock = MUTEX_INIT;
static enum kind kind = TEXT;
static uint32_t width, height, pitch, depth;
static uint8_t *pixels; /* kernel address of the visible framebuffer */
static uint32_t bytes;
static uint32_t mapped_pages;
static uint32_t mode_generation; /* counts mode changes (for the refresher) */

static bool bga_present;
static uintptr_t bga_phys;
static uint32_t bga_memory;
static const char *bga_name;

static uint16_t dispi_read(uint16_t index)
{
	outw(VBE_INDEX, index);
	return inw(VBE_DATA);
}

static void dispi_write(uint16_t index, uint16_t value)
{
	outw(VBE_INDEX, index);
	outw(VBE_DATA, value);
}

static void unmap_lfb(void)
{
	for (uint32_t i = 0; i < mapped_pages; i++) {
		uintptr_t phys;
		vmm_unmap_page(KERNEL_FB_START + i * PAGE_SIZE, &phys); /* device memory: not the PMM's */
	}
	mapped_pages = 0;
}

static int map_lfb(uintptr_t phys, uint32_t len)
{
	uint32_t pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
	for (uint32_t i = 0; i < pages; i++) {
		if (vmm_map_page(KERNEL_FB_START + i * PAGE_SIZE, phys + i * PAGE_SIZE, VMM_WRITABLE) != 0) {
			unmap_lfb();
			return -ENOMEM;
		}
		mapped_pages = i + 1;
	}
	return 0;
}

/* Leaves whatever graphics mode is on, without touching text state. */
static void leave_graphics(void)
{
	if (kind == BGA) {
		dispi_write(VBE_ENABLE, 0);
		unmap_lfb();
	}
	pixels = 0;
	bytes = 0;
}

static void enter_graphics(void)
{
	if (kind == TEXT) {
		vga_text_suspend();
		vga_save_text();
	} else {
		leave_graphics();
	}
}

static int set_text(void)
{
	if (kind != TEXT) {
		leave_graphics();
		vga_restore_text();
		vga_text_resume();
		kind = TEXT;
	}
	return 0;
}

/* VirtualBox can repaint what is drawn just after a mode change with
 * the previous mode's line length, leaving most of the screen as it was
 * (seen in VirtualBox going from the desktop's 800x600 to gfxdemo's
 * 640x480: only the first 640 bytes of every 3200, the old line, were
 * repainted). An emulator repaints the pages that were written to, so for
 * a while after each change a thread writes every page of the frame
 * again, unchanged; by then the emulator has the new mode. Harmless
 * elsewhere: a few hundred reads and writes. */
static const uint32_t REFRESH_AFTER_MS[] = { 50, 200, 600, 1500 };

static void refresher_main(void *arg)
{
	uint32_t generation = (uint32_t)(uintptr_t)arg, slept = 0;
	for (size_t i = 0; i < sizeof(REFRESH_AFTER_MS) / sizeof(REFRESH_AFTER_MS[0]); i++) {
		timer_sleep_ms(REFRESH_AFTER_MS[i] - slept);
		slept = REFRESH_AFTER_MS[i];
		mutex_lock(&lock);
		bool current = generation == mode_generation && kind == BGA && pixels;
		for (uint32_t off = 0; current && off < bytes; off += PAGE_SIZE) {
			volatile uint32_t *p = (volatile uint32_t *)(pixels + off);
			*p = *p;
		}
		mutex_unlock(&lock);
		if (!current) {
			break; /* another mode since: its own thread refreshes it */
		}
	}
	thread_exit();
}

static void start_refresher(void)
{
	mode_generation++;
	thread_create("fbrefresh", refresher_main, (void *)(uintptr_t)mode_generation);
}

static int set_bga(uint32_t w, uint32_t h)
{
	if (!bga_present) {
		return -ENODEV;
	}
	if (w < 320 || h < 200 || w > 1600 || h > 1200 || w % 8 || (uint64_t)w * h * 4 > bga_memory) {
		return -EINVAL;
	}
	enter_graphics();
	kind = BGA;
	/* In the order VirtualBox's own VGA BIOS uses, the line length given
	 * outright rather than left to the adapter to derive. */
	dispi_write(VBE_ENABLE, 0);
	dispi_write(VBE_BPP, 32);
	dispi_write(VBE_XRES, (uint16_t)w);
	dispi_write(VBE_YRES, (uint16_t)h);
	dispi_write(VBE_BANK, 0);
	dispi_write(VBE_VIRT_WIDTH, (uint16_t)w);
	dispi_write(VBE_VIRT_HEIGHT, (uint16_t)h);
	dispi_write(VBE_X_OFFSET, 0);
	dispi_write(VBE_Y_OFFSET, 0);
	dispi_write(VBE_ENABLE, VBE_ENABLED | VBE_LFB);
	width = dispi_read(VBE_XRES);
	height = dispi_read(VBE_YRES);
	pitch = dispi_read(VBE_VIRT_WIDTH) * 4;
	depth = 32;
	bytes = pitch * height;
	if (width != w || height != h || map_lfb(bga_phys, bytes) != 0) {
		set_text();
		return -EINVAL;
	}
	pixels = (uint8_t *)KERNEL_FB_START;
	memset(pixels, 0, bytes);
	start_refresher();
	return 0;
}

static int set_vga13(void)
{
	enter_graphics();
	kind = VGA13;
	vga_set_mode13();
	width = 320;
	height = 200;
	pitch = 320;
	depth = 8;
	bytes = pitch * height;
	pixels = P2V(0xA0000);
	return 0;
}

static int describe(char *buf, size_t size)
{
	if (kind == TEXT) {
		return ksnprintf(buf, size, "text\n");
	}
	return ksnprintf(buf, size, "%lu %lu %lu %lu %s %s\n", (unsigned long)width,
	                 (unsigned long)height, (unsigned long)depth, (unsigned long)pitch,
	                 kind == BGA ? "xrgb8888" : "rgb332", kind == BGA ? "bga" : "vga");
}

static long ctl_pread(struct device *dev, uint32_t offset, void *buf, size_t len)
{
	(void)dev;
	char text[64];
	mutex_lock(&lock);
	int n = describe(text, sizeof(text));
	mutex_unlock(&lock);
	if (offset >= (uint32_t)n) {
		return 0;
	}
	size_t k = (size_t)n - offset < len ? (size_t)n - offset : len;
	memcpy(buf, text + offset, k);
	return (long)k;
}

static bool parse_number(const char **p, uint32_t *out)
{
	uint32_t v = 0;
	const char *s = *p;
	while (*s == ' ') {
		s++;
	}
	const char *start = s;
	while (*s >= '0' && *s <= '9' && v < 100000) {
		v = v * 10 + (uint32_t)(*s++ - '0');
	}
	*p = s;
	*out = v;
	return s != start;
}

static long ctl_pwrite(struct device *dev, uint32_t offset, const void *buf, size_t len)
{
	(void)dev;
	(void)offset;
	char cmd[32];
	if (len >= sizeof(cmd)) {
		return -EINVAL;
	}
	memcpy(cmd, buf, len);
	cmd[len] = '\0';
	while (len && (cmd[len - 1] == '\n' || cmd[len - 1] == ' ')) {
		cmd[--len] = '\0';
	}
	int rc = -EINVAL;
	mutex_lock(&lock);
	if (!strcmp(cmd, "text")) {
		rc = set_text();
	} else if (!strcmp(cmd, "mode vga")) {
		rc = set_vga13();
	} else if (!strncmp(cmd, "mode ", 5)) {
		const char *p = cmd + 5;
		uint32_t w, h;
		if (parse_number(&p, &w) && parse_number(&p, &h) && !*p) {
			rc = set_bga(w, h);
		}
	}
	mutex_unlock(&lock);
	return rc ? rc : (long)len;
}

static long fb_pread(struct device *dev, uint32_t offset, void *buf, size_t len)
{
	(void)dev;
	mutex_lock(&lock);
	long rc;
	if (kind == TEXT) {
		rc = -ENXIO;
	} else if (offset >= bytes) {
		rc = 0;
	} else {
		rc = (long)(len < bytes - offset ? len : bytes - offset);
		memcpy(buf, pixels + offset, (size_t)rc);
	}
	mutex_unlock(&lock);
	return rc;
}

static long fb_pwrite(struct device *dev, uint32_t offset, const void *buf, size_t len)
{
	(void)dev;
	mutex_lock(&lock);
	long rc;
	if (kind == TEXT) {
		rc = -ENXIO;
	} else if (offset >= bytes) {
		rc = -ENXIO;
	} else {
		rc = (long)(len < bytes - offset ? len : bytes - offset);
		memcpy(pixels + offset, buf, (size_t)rc);
	}
	mutex_unlock(&lock);
	return rc;
}

static uint32_t fb_size(struct device *dev)
{
	(void)dev;
	return bytes;
}

static const struct char_device_ops fb_ops = { .pread = fb_pread, .pwrite = fb_pwrite, .size = fb_size };
static const struct char_device_ops ctl_ops = { .pread = ctl_pread, .pwrite = ctl_pwrite };
static struct device fb_device = { .name = "fb", .class = DEVICE_CHAR, .char_ops = &fb_ops };
static struct device ctl_device = { .name = "fbctl", .class = DEVICE_CHAR, .char_ops = &ctl_ops };

/* The size of memory BAR `i` (the standard probe: write all ones, read
 * back which address bits stick), with memory decoding off meanwhile. */
static uint32_t bar_size(const struct pci_device *d, int i)
{
	uint8_t reg = (uint8_t)(PCI_BAR0 + 4 * i);
	uint32_t command = pci_read32(d, PCI_COMMAND) & 0xFFFF; /* not the status bits */
	pci_write32(d, PCI_COMMAND, command & ~(uint32_t)PCI_COMMAND_MEMORY);
	pci_write32(d, reg, 0xFFFFFFFFu);
	uint32_t mask = pci_read32(d, reg) & PCI_BAR_MEM_MASK;
	pci_write32(d, reg, d->bar[i]);
	pci_write32(d, PCI_COMMAND, command);
	return mask ? ~mask + 1 : 0;
}

/* The adapter's video memory: its first prefetchable memory BAR (BAR 0 on
 * the Bochs and VirtualBox VGA adapters, BAR 1 after the I/O ports on
 * SVGA II), or failing that its first memory BAR. -1 if none. */
static int vram_bar(const struct pci_device *d)
{
	int first = -1;
	for (int i = 0; i < 6; i++) {
		uint32_t bar = d->bar[i];
		if (bar & PCI_BAR_IO || (bar & PCI_BAR_MEM_MASK) == 0) {
			continue;
		}
		if (bar & PCI_BAR_PREFETCH) {
			return i;
		}
		if (first < 0) {
			first = i;
		}
		if (bar & PCI_BAR_64BIT) {
			i++; /* the high half; ManiOS can't reach it anyway */
		}
	}
	return first;
}

void fb_init(void)
{
	const struct pci_device *pci = 0;
	for (size_t i = 0; !pci && i < sizeof(ADAPTERS) / sizeof(ADAPTERS[0]); i++) {
		pci = pci_find(ADAPTERS[i].vendor, ADAPTERS[i].device);
		bga_name = ADAPTERS[i].name;
	}
	uint16_t id = dispi_read(VBE_ID);
	int bar = pci ? vram_bar(pci) : -1;
	if (pci && bar >= 0 && id >= 0xB0C0 && id <= 0xB0C5) {
		bga_present = true;
		bga_phys = pci->bar[bar] & PCI_BAR_MEM_MASK;
		/* The BAR's size is the video memory; version 5 of the registers
		 * can say how much of it there is too. */
		bga_memory = bar_size(pci, bar);
		if (id >= 0xB0C5) {
			uint32_t said = (uint32_t)dispi_read(VBE_MEMORY_64K) * 65536;
			if (said && (said < bga_memory || !bga_memory)) {
				bga_memory = said;
			}
		}
		if (!bga_memory) {
			bga_memory = 4u << 20;
		}
		if (bga_memory > KERNEL_FB_SIZE) {
			bga_memory = KERNEL_FB_SIZE;
		}
		kprintf("fb: %s at pci %02x:%02x.%x, framebuffer 0x%08lx, %lu KiB\n", bga_name, pci->bus,
		        pci->dev, pci->fn, (unsigned long)bga_phys, (unsigned long)(bga_memory / 1024));
	} else if (pci) {
		kprintf("fb: %s at pci %02x:%02x.%x lacks the Bochs VBE registers (id 0x%04x)\n", bga_name,
		        pci->bus, pci->dev, pci->fn, id);
	}
	device_register(&fb_device);
	device_register(&ctl_device);
}

void fb_emergency_text(void)
{
	/* No lock: a panic may have interrupted a holder. */
	if (kind == BGA) {
		dispi_write(VBE_ENABLE, 0);
	}
	if (kind != TEXT) {
		vga_restore_text();
		vga_text_resume();
		kind = TEXT;
	}
}
