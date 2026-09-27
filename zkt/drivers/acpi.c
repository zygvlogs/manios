/* ACPI power control: turning the machine off (sleep state S5) and
 * resetting it, written from the ACPI specification (6.x, and the 1.0
 * structures it keeps). Device "power": writing "off" turns the machine
 * off, "reboot" resets it; reading says how.
 *
 * The firmware's tables are found from the Root System Description
 * Pointer ("RSD PTR ", in the EBDA's first KiB or the BIOS area
 * 0xE0000-0xFFFFF): the RSDT lists the tables, the FADT ("FACP") gives
 * the PM1 control registers, the reset register and the DSDT. S5's
 * SLP_TYP values are in the DSDT's \_S5 package, which is found as its
 * AML bytes (NameOp "_S5_" PackageOp ...), not by running AML: this
 * kernel has no AML interpreter. Going to S5 is SLP_TYP and SLP_EN
 * written to PM1a (and PM1b) control, with ACPI mode enabled first
 * through SMI_CMD if the firmware hasn't.
 *
 * Resetting tries, in turn, the FADT's reset register (ACPI 2.0 on),
 * the keyboard controller's reset line (0xFE to port 0x64) and a
 * triple fault. */
#include "acpi.h"
#include <stdbool.h>
#include <stdint.h>
#include "cpu.h"
#include "device.h"
#include "io.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "memlayout.h"
#include "mmio.h"
#include "timer.h"

#define SLP_EN (1u << 13)
#define SCI_EN 1u
#define FADT_RESET_SUPPORTED (1u << 10)
#define GAS_MEMORY 0
#define GAS_IO 1

static struct {
	bool found;
	uint16_t pm1a, pm1b;       /* PM1 control ports (0: none) */
	uint16_t slp_typa, slp_typb;
	bool have_s5;
	uint32_t smi_cmd;
	uint8_t acpi_enable;
	bool reset_supported;
	uint8_t reset_space, reset_value;
	uint64_t reset_address;
	char oem[7];
	uint8_t revision;
} acpi;

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool checksum_ok(const uint8_t *p, uint32_t len)
{
	uint8_t sum = 0;
	for (uint32_t i = 0; i < len; i++) {
		sum = (uint8_t)(sum + p[i]);
	}
	return sum == 0;
}

/* Physical memory to read: the boot mapping's, below 16 MiB, or a new
 * mapping (tables usually sit at the top of memory). */
static const uint8_t *map(uint32_t phys, uint32_t len)
{
	if (phys + len <= BOOT_MAPPED_PHYS_LIMIT && phys + len > phys) {
		return P2V(phys);
	}
	return (const uint8_t *)mmio_map(phys, len);
}

/* A whole table, its signature checked, or NULL. */
static const uint8_t *table(uint32_t phys, const char *signature)
{
	const uint8_t *head = map(phys, 36);
	if (!head || memcmp(head, signature, 4) != 0) {
		return 0;
	}
	uint32_t len = le32(head + 4);
	if (len < 36 || len > 1024 * 1024) {
		return 0;
	}
	const uint8_t *t = map(phys, len);
	return t && checksum_ok(t, len) ? t : 0;
}

static const uint8_t *find_rsdp(void)
{
	uint32_t ebda = (uint32_t)*(const uint16_t *)P2V(0x40E) << 4;
	uint32_t areas[2][2] = { { ebda, ebda + 1024 }, { 0xE0000, 0x100000 } };
	for (int a = 0; a < 2; a++) {
		if (areas[a][0] < 0x80000 && a == 0) {
			continue; /* no EBDA where one could be */
		}
		for (uint32_t p = areas[a][0]; p + 20 <= areas[a][1]; p += 16) {
			const uint8_t *r = P2V(p);
			if (!memcmp(r, "RSD PTR ", 8) && checksum_ok(r, 20)) {
				return r;
			}
		}
	}
	return 0;
}

/* One element of an AML package: a byte (BytePrefix, 0x0A), Zero, One. */
static bool aml_byte(const uint8_t **p, const uint8_t *end, uint16_t *out)
{
	if (*p >= end) {
		return false;
	}
	if (**p == 0x0A && *p + 1 < end) {
		*out = (*p)[1];
		*p += 2;
		return true;
	}
	if (**p == 0x00 || **p == 0x01) {
		*out = **p;
		*p += 1;
		return true;
	}
	return false;
}

/* \_S5's first two elements: SLP_TYPa and SLP_TYPb. */
static bool find_s5(const uint8_t *dsdt, uint32_t len)
{
	const uint8_t *end = dsdt + len;
	for (uint32_t i = 38; i + 8 < len; i++) {
		if (memcmp(dsdt + i, "_S5_", 4) != 0
		    || !(dsdt[i - 1] == 0x08 || (dsdt[i - 2] == 0x08 && dsdt[i - 1] == '\\'))) {
			continue;
		}
		const uint8_t *p = dsdt + i + 4;
		if (*p++ != 0x12) { /* PackageOp */
			continue;
		}
		p += ((*p >> 6) & 3) + 1; /* the package's length: 1 to 4 bytes */
		p++;                      /* NumElements */
		if (aml_byte(&p, end, &acpi.slp_typa) && aml_byte(&p, end, &acpi.slp_typb)) {
			return true;
		}
	}
	return false;
}

static void power_off(void)
{
	if (!acpi.found || !acpi.have_s5 || !acpi.pm1a) {
		return;
	}
	if (acpi.smi_cmd && acpi.acpi_enable && !(inw(acpi.pm1a) & SCI_EN)) {
		outb((uint16_t)acpi.smi_cmd, acpi.acpi_enable);
		for (int i = 0; i < 300 && !(inw(acpi.pm1a) & SCI_EN); i++) {
			timer_sleep_ms(10);
		}
	}
	(void)cpu_irq_save();
	outw(acpi.pm1a, (uint16_t)(acpi.slp_typa << 10 | SLP_EN));
	if (acpi.pm1b) {
		outw(acpi.pm1b, (uint16_t)(acpi.slp_typb << 10 | SLP_EN));
	}
	for (int i = 0; i < 1000000; i++) {
		io_wait(); /* the machine goes off in the meantime */
	}
	cpu_enable_interrupts();
}

static void reset(void)
{
	(void)cpu_irq_save();
	if (acpi.reset_supported && acpi.reset_space == GAS_IO) {
		outb((uint16_t)acpi.reset_address, acpi.reset_value);
	} else if (acpi.reset_supported && acpi.reset_space == GAS_MEMORY
	           && acpi.reset_address < 0x100000000ull) {
		volatile uint8_t *r = (volatile uint8_t *)map((uint32_t)acpi.reset_address, 1);
		if (r) {
			*r = acpi.reset_value;
		}
	}
	for (int i = 0; i < 100000; i++) {
		io_wait();
	}
	/* The keyboard controller's pulse on the reset line. */
	for (int i = 0; i < 100000 && (inb(0x64) & 0x02); i++) {
	}
	outb(0x64, 0xFE);
	for (int i = 0; i < 100000; i++) {
		io_wait();
	}
	/* A triple fault: no interrupt table, then an interrupt. */
	static const struct {
		uint16_t limit;
		uint32_t base;
	} __attribute__((packed)) none = { 0, 0 };
	__asm__ volatile("lidt %0; int $3" : : "m"(none));
	for (;;) {
		__asm__ volatile("hlt");
	}
}

static long power_write(struct device *dev, uint32_t offset, const void *buf, size_t len)
{
	(void)dev;
	(void)offset;
	char word[8] = "";
	size_t n = len < sizeof(word) - 1 ? len : sizeof(word) - 1;
	memcpy(word, buf, n);
	while (n && (word[n - 1] == '\n' || word[n - 1] == ' ')) {
		word[--n] = '\0';
	}
	if (!strcmp(word, "off")) {
		kprintf("power: turning the machine off\n");
		timer_sleep_ms(50); /* the message goes out */
		power_off();
		return -ENODEV; /* still here: no way to turn it off */
	}
	if (!strcmp(word, "reboot")) {
		kprintf("power: resetting the machine\n");
		timer_sleep_ms(50);
		reset();
	}
	return -EINVAL;
}

static long power_read(struct device *dev, uint32_t offset, void *buf, size_t len)
{
	(void)dev;
	char text[160];
	int n;
	if (!acpi.found) {
		n = ksnprintf(text, sizeof(text), "no ACPI: reboot by the keyboard controller; no off\n");
	} else {
		n = ksnprintf(text, sizeof(text), "ACPI %s (%s): off %s%x (SLP_TYP %u); reboot by %s\n",
		              acpi.revision >= 2 ? "2.0 or later" : "1.0", acpi.oem,
		              acpi.have_s5 ? "by PM1a 0x" : "unknown, PM1a 0x", acpi.pm1a, acpi.slp_typa,
		              acpi.reset_supported ? "the reset register" : "the keyboard controller");
	}
	if (offset >= (uint32_t)n) {
		return 0;
	}
	if (len > (size_t)n - offset) {
		len = (size_t)n - offset;
	}
	memcpy(buf, text + offset, len);
	return (long)len;
}

static const struct char_device_ops power_ops = { .pread = power_read, .pwrite = power_write };
static struct device power_device = { .name = "power", .class = DEVICE_CHAR, .char_ops = &power_ops };

void acpi_init(void)
{
	const uint8_t *rsdp = find_rsdp();
	const uint8_t *rsdt = rsdp ? table(le32(rsdp + 16), "RSDT") : 0;
	const uint8_t *fadt = 0;
	for (uint32_t i = 36; rsdt && i + 4 <= le32(rsdt + 4) && !fadt; i += 4) {
		fadt = table(le32(rsdt + i), "FACP");
	}
	if (fadt) {
		uint32_t len = le32(fadt + 4);
		acpi.found = true;
		acpi.revision = rsdp[15];
		memcpy(acpi.oem, rsdp + 9, 6);
		for (int i = 5; i >= 0 && acpi.oem[i] == ' '; i--) {
			acpi.oem[i] = '\0';
		}
		acpi.smi_cmd = le32(fadt + 48);
		acpi.acpi_enable = fadt[52];
		acpi.pm1a = (uint16_t)le32(fadt + 64);
		acpi.pm1b = (uint16_t)le32(fadt + 68);
		if (len >= 129 && (le32(fadt + 112) & FADT_RESET_SUPPORTED)) {
			acpi.reset_supported = true;
			acpi.reset_space = fadt[116];
			acpi.reset_address = (uint64_t)le32(fadt + 120) | (uint64_t)le32(fadt + 124) << 32;
			acpi.reset_value = fadt[128];
		}
		const uint8_t *dsdt = table(le32(fadt + 40), "DSDT");
		acpi.have_s5 = dsdt && find_s5(dsdt, le32(dsdt + 4));
		kprintf("acpi: %s, PM1a at 0x%x, S5 %s, reset %s\n", acpi.oem, acpi.pm1a,
		        acpi.have_s5 ? "found" : "not found",
		        acpi.reset_supported ? "by register" : "by keyboard controller");
	}
	device_register(&power_device);
}
