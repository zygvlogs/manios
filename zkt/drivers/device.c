#include "device.h"
#include "cpu.h"
#include "kerrno.h"
#include "kstring.h"

static struct device *head, *tail;

int device_register(struct device *dev)
{
	int rc = 0;
	uint32_t flags = cpu_irq_save();
	if (device_find(dev->name)) {
		rc = -EEXIST;
	} else {
		dev->next = 0;
		if (tail) {
			tail->next = dev;
		} else {
			head = dev;
		}
		tail = dev;
	}
	cpu_irq_restore(flags);
	return rc;
}

struct device *device_find(const char *name)
{
	for (struct device *d = head; d; d = d->next) {
		if (strcmp(d->name, name) == 0) {
			return d;
		}
	}
	return 0;
}

struct device *device_next(struct device *prev)
{
	return prev ? prev->next : head;
}

long device_read(struct device *dev, void *buf, size_t len)
{
	if (dev->class != DEVICE_CHAR || !dev->char_ops->read) {
		return -ENODEV;
	}
	return dev->char_ops->read(dev, buf, len);
}

long device_write(struct device *dev, const void *buf, size_t len)
{
	if (dev->class != DEVICE_CHAR || !dev->char_ops->write) {
		return -ENODEV;
	}
	return dev->char_ops->write(dev, buf, len);
}

int device_write_blocks(struct device *dev, uint32_t lba, uint32_t count, const void *buf)
{
	if (dev->class != DEVICE_BLOCK || !dev->block_ops->write) {
		return -ENODEV;
	}
	if (lba > dev->block_count || count > dev->block_count - lba) {
		return -ENXIO;
	}
	return count ? dev->block_ops->write(dev, lba, count, buf) : 0;
}

int device_read_blocks(struct device *dev, uint32_t lba, uint32_t count, void *buf)
{
	if (dev->class != DEVICE_BLOCK || !dev->block_ops->read) {
		return -ENODEV;
	}
	if (lba > dev->block_count || count > dev->block_count - lba) {
		return -ENXIO;
	}
	return count ? dev->block_ops->read(dev, lba, count, buf) : 0;
}
