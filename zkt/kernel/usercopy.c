#include "usercopy.h"
#include <stdint.h>
#include "kerrno.h"
#include "kstring.h"
#include "memlayout.h"
#include "vmm.h"

int copy_from_user(void *dst, const void *user_src, size_t n)
{
	if (!vmm_user_range_ok((uintptr_t)user_src, n, false)) {
		return -EFAULT;
	}
	memcpy(dst, user_src, n);
	return 0;
}

int copy_to_user(void *user_dst, const void *src, size_t n)
{
	if (!vmm_user_range_ok((uintptr_t)user_dst, n, true)) {
		return -EFAULT;
	}
	memcpy(user_dst, src, n);
	return 0;
}

long copy_string_from_user(char *dst, const char *user_src, size_t max)
{
	uintptr_t p = (uintptr_t)user_src;
	size_t len = 0;
	if (max == 0) {
		return -ENAMETOOLONG;
	}
	while (len < max) {
		/* Check a page at a time: the string may end before an unmapped page. */
		size_t in_page = PAGE_SIZE - (p + len) % PAGE_SIZE;
		if (!vmm_user_range_ok(p + len, 1, false)) {
			return -EFAULT;
		}
		for (size_t i = 0; i < in_page && len < max; i++, len++) {
			dst[len] = user_src[len];
			if (!dst[len]) {
				return (long)len;
			}
		}
	}
	dst[max - 1] = '\0';
	return -ENAMETOOLONG;
}
