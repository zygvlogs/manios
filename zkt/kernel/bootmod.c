/* Boot modules as devices (bootmod.h). */
#include "bootmod.h"
#include "device.h"
#include "heap.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "memlayout.h"
#include "vmm.h"

struct module_device {
	struct device dev;
	const uint8_t *data;
	uint32_t size;
};

static long module_read(struct device *dev, uint32_t offset, void *buf, size_t len)
{
	struct module_device *m = (struct module_device *)dev;
	if (offset >= m->size) {
		return 0;
	}
	if (len > m->size - offset) {
		len = m->size - offset;
	}
	memcpy(buf, m->data + offset, len);
	return (long)len;
}

static uint32_t module_size(struct device *dev)
{
	return ((struct module_device *)dev)->size;
}

static const struct char_device_ops module_ops = { .pread = module_read, .size = module_size };

void bootmod_init(const struct boot_module *mods, size_t count)
{
	uintptr_t next = KERNEL_MODULES_START;
	for (size_t i = 0; i < count; i++) {
		const struct boot_module *b = &mods[i];
		uintptr_t first = b->start & ~(uintptr_t)(PAGE_SIZE - 1);
		size_t pages = (b->end - first + PAGE_SIZE - 1) / PAGE_SIZE;
		if (next + pages * PAGE_SIZE > KERNEL_MODULES_START + KERNEL_MODULES_SIZE) {
			kprintf("bootmod: %s: no room to map %lu KiB\n", b->name,
			        (unsigned long)(pages * 4));
			continue;
		}
		struct module_device *m = kmalloc(sizeof(*m));
		if (!m) {
			continue;
		}
		memset(m, 0, sizeof(*m));
		bool mapped = true;
		for (size_t p = 0; p < pages && mapped; p++) {
			mapped = vmm_map_page(next + p * PAGE_SIZE, first + p * PAGE_SIZE, 0) == 0;
		}
		if (!mapped) {
			kfree(m);
			continue;
		}
		strlcpy(m->dev.name, b->name, sizeof(m->dev.name));
		m->dev.class = DEVICE_CHAR;
		m->dev.char_ops = &module_ops;
		m->data = (const uint8_t *)(next + (b->start - first));
		m->size = b->end - b->start;
		next += pages * PAGE_SIZE;
		if (device_register(&m->dev) != 0) {
			kprintf("bootmod: %s: a device by that name exists\n", b->name);
			continue;
		}
		kprintf("bootmod: /dev/%s, %lu KiB\n", m->dev.name, (unsigned long)(m->size / 1024));
	}
}
