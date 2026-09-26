#ifndef MANIOS_SYS_TYPES_H
#define MANIOS_SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <sys/cdefs.h>

#ifndef MANIOS_SSIZE_T
#define MANIOS_SSIZE_T
typedef long ssize_t; /* what read() and write() return */
#endif
typedef long off_t;
typedef int pid_t;
/* For struct stat (<sys/stat.h>). */
typedef uint32_t dev_t;
typedef uint32_t ino_t;
typedef uint32_t mode_t;
typedef uint32_t nlink_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int32_t blksize_t;
typedef int64_t blkcnt_t;
typedef int64_t time_t;
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;

#endif
