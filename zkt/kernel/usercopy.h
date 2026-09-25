#ifndef ZKT_KERNEL_USERCOPY_H
#define ZKT_KERNEL_USERCOPY_H

#include <stddef.h>

/* Copies between the kernel and the calling process's memory. The whole
 * user range is checked first (user space, mapped, user-accessible, and
 * writable for copy_to_user), so a bad pointer from a program is -EFAULT
 * rather than a kernel page fault. Processes have one thread, so nothing
 * can unmap the range between the check and the copy. */
int copy_from_user(void *dst, const void *user_src, size_t n);
int copy_to_user(void *user_dst, const void *src, size_t n);

/* Copies a NUL-terminated string of at most max - 1 characters; returns
 * its length, -EFAULT, or -ENAMETOOLONG if it doesn't fit. */
long copy_string_from_user(char *dst, const char *user_src, size_t max);

#endif
