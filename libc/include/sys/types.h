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
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;

#endif
