/* Device "nvram": the CMOS memory beside the clock (the MC146818's), from
 * byte 14 on -- the 114 bytes after the clock's registers, where the
 * BIOS keeps its settings: the floppy drive types, the memory size, the
 * boot order... -- as a file of 114 bytes, read and written in place,
 * like the /dev/nvram of other systems. A write to the PC/AT's summed
 * range (0x10-0x2D) makes the checksum at 0x2E-0x2F right again, as the
 * BIOS would reject the settings otherwise. */
#include "nvram.h"
#include "device.h"
#include "kerrno.h"
#include "mutex.h"
#include "rtc.h"

#define FIRST 14
#define BYTES 114
#define SUM_FIRST 0x10
#define SUM_LAST  0x2D
#define SUM_HIGH  0x2E
#define SUM_LOW   0x2F

static struct mutex lock = MUTEX_INIT;

static long nvram_read(struct device *dev, uint32_t offset, void *buf, size_t len)
{
	(void)dev;
	uint8_t *out = buf;
	if (offset >= BYTES) {
		return 0;
	}
	if (len > BYTES - offset) {
		len = BYTES - offset;
	}
	mutex_lock(&lock);
	for (size_t i = 0; i < len; i++) {
		out[i] = cmos_read((uint8_t)(FIRST + offset + i));
	}
	mutex_unlock(&lock);
	return (long)len;
}

static long nvram_write(struct device *dev, uint32_t offset, const void *buf, size_t len)
{
	(void)dev;
	const uint8_t *in = buf;
	if (offset >= BYTES) {
		return len ? -ENXIO : 0;
	}
	if (len > BYTES - offset) {
		len = BYTES - offset;
	}
	mutex_lock(&lock);
	for (size_t i = 0; i < len; i++) {
		cmos_write((uint8_t)(FIRST + offset + i), in[i]);
	}
	uint32_t first = FIRST + offset, last = first + (uint32_t)len - 1;
	if (last >= SUM_FIRST && first <= SUM_LAST) {
		uint16_t sum = 0;
		for (uint8_t r = SUM_FIRST; r <= SUM_LAST; r++) {
			sum = (uint16_t)(sum + cmos_read(r));
		}
		cmos_write(SUM_HIGH, (uint8_t)(sum >> 8));
		cmos_write(SUM_LOW, (uint8_t)sum);
	}
	mutex_unlock(&lock);
	return (long)len;
}

static uint32_t nvram_size(struct device *dev)
{
	(void)dev;
	return BYTES;
}

static const struct char_device_ops nvram_ops = {
	.pread = nvram_read, .pwrite = nvram_write, .size = nvram_size,
};
static struct device nvram_device = { .name = "nvram", .class = DEVICE_CHAR, .char_ops = &nvram_ops };

void nvram_register(void)
{
	device_register(&nvram_device);
}
