/* Kernel error codes, returned negated (return -ENOENT). The numbers
 * live in the ABI header, since system calls return them to userspace. */
#ifndef ZKT_KERNEL_KERRNO_H
#define ZKT_KERNEL_KERRNO_H

#include "zkt_abi.h" /* the numbers are part of the userspace ABI */

/* Short description for messages; takes the positive or negated code. */
const char *kstrerror(int err);

#endif
