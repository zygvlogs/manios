/* The driver framework (FOUNDING-PROPOSAL §2.7): each driver fills in a
 * struct device with the operations table for its class and registers
 * it by name. Devfs (M7) exposes every registered device as a file. */
#ifndef ZKT_DRIVERS_DEVICE_H
#define ZKT_DRIVERS_DEVICE_H

#include <stddef.h>
#include <stdint.h>

#define DEVICE_NAME_MAX 16

enum device_class {
	DEVICE_CHAR,  /* byte stream: console, serial line, display */
	DEVICE_BLOCK, /* fixed-size blocks addressed by number: disks */
};

struct device;

/* A NULL operation means the device doesn't support it (-ENODEV). */
struct char_device_ops {
	/* Blocks until at least one byte is available; returns the number of
	 * bytes read, or a negated error. */
	long (*read)(struct device *dev, void *buf, size_t len);
	long (*write)(struct device *dev, const void *buf, size_t len);
	/* Optional, for devices addressed by byte offset (the framebuffer):
	 * used instead of read/write when present, and `size` gives the
	 * file size devfs reports. */
	long (*pread)(struct device *dev, uint32_t offset, void *buf, size_t len);
	long (*pwrite)(struct device *dev, uint32_t offset, const void *buf, size_t len);
	uint32_t (*size)(struct device *dev);
	/* Optional: devfs calls open and close as a /dev file is opened and
	 * its last reference goes; poll says whether a read would not block
	 * (called with interrupts off). */
	void (*open)(struct device *dev);
	void (*close)(struct device *dev);
	int (*poll)(struct device *dev);
};

struct block_device_ops {
	/* Transfer `count` blocks starting at block `lba`; 0 or a negated
	 * error. The caller keeps lba + count <= block_count. */
	int (*read)(struct device *dev, uint32_t lba, uint32_t count, void *buf);
	int (*write)(struct device *dev, uint32_t lba, uint32_t count, const void *buf);
};

struct device {
	char name[DEVICE_NAME_MAX];
	enum device_class class;
	const struct char_device_ops *char_ops;
	const struct block_device_ops *block_ops;
	uint32_t block_size;  /* bytes; block devices only */
	uint32_t block_count; /* 32 bits: 2 TiB at 512-byte blocks */
	void *driver_data;
	struct device *next;
};

/* Returns 0, or -EEXIST if the name is taken. The device must stay
 * allocated for as long as the kernel runs (there is no unregister). */
int device_register(struct device *dev);

struct device *device_find(const char *name);

/* Iterates registered devices in registration order: pass NULL first. */
struct device *device_next(struct device *prev);

/* Class-checked calls that turn a missing operation into -ENODEV and
 * an out-of-range block request into -ENXIO. */
long device_read(struct device *dev, void *buf, size_t len);
long device_write(struct device *dev, const void *buf, size_t len);
int device_read_blocks(struct device *dev, uint32_t lba, uint32_t count, void *buf);
int device_write_blocks(struct device *dev, uint32_t lba, uint32_t count, const void *buf);

#endif
