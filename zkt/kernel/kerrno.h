/* Kernel error codes, returned negated (return -ENOENT). The numbers
 * follow traditional Unix values so they read familiarly in dumps. */
#ifndef ZKT_KERNEL_KERRNO_H
#define ZKT_KERNEL_KERRNO_H

#define EPERM         1
#define ENOENT        2
#define EIO           5
#define ENXIO         6
#define EBADF         9
#define ENOMEM       12
#define EBUSY        16
#define EEXIST       17
#define ENODEV       19
#define ENOTDIR      20
#define EISDIR       21
#define EINVAL       22
#define EROFS        30
#define ENAMETOOLONG 36

/* Short description for messages; takes the positive or negated code. */
const char *kstrerror(int err);

#endif
