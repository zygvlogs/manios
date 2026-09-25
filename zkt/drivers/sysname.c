/* Device "sysname" (sysname.h). */
#include "sysname.h"
#include "cmdline.h"
#include "device.h"
#include "kstring.h"

#define NAME_MAX 32

static char text[NAME_MAX + 2]; /* the name and a newline */

static long sysname_read(struct device *dev, uint32_t offset, void *buf, size_t len)
{
	(void)dev;
	size_t n = strlen(text);
	if (offset >= n) {
		return 0;
	}
	if (len > n - offset) {
		len = n - offset;
	}
	memcpy(buf, text + offset, len);
	return (long)len;
}

static const struct char_device_ops sysname_ops = { .pread = sysname_read };
static struct device sysname_device = {
	.name = "sysname", .class = DEVICE_CHAR, .char_ops = &sysname_ops
};

void sysname_register(void)
{
	char name[NAME_MAX + 1];
	if (!cmdline_get("sysname", name, sizeof(name)) || !name[0]) {
		strlcpy(name, "manios", sizeof(name));
	}
	strlcpy(text, name, sizeof(text) - 1);
	size_t n = strlen(text);
	text[n] = '\n';
	text[n + 1] = '\0';
	device_register(&sysname_device);
}
