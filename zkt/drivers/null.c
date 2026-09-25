/* Device "null": reads end at once, writes vanish. */
#include "null.h"
#include "device.h"

static long null_read(struct device *dev, void *buf, size_t len)
{
	(void)dev;
	(void)buf;
	(void)len;
	return 0;
}

static long null_write(struct device *dev, const void *buf, size_t len)
{
	(void)dev;
	(void)buf;
	return (long)len;
}

static const struct char_device_ops null_ops = { .read = null_read, .write = null_write };
static struct device null_device = { .name = "null", .class = DEVICE_CHAR, .char_ops = &null_ops };

void null_register(void)
{
	device_register(&null_device);
}
