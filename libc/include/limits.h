/* The compiler's <limits.h> (the integer types), and ManiOS's limits. */
#ifndef MANIOS_LIMITS_H
#define MANIOS_LIMITS_H

#include_next <limits.h>
#include <zkt_abi.h>

#define PATH_MAX (ZKT_PATH_MAX + 1) /* with its NUL */
#define LINE_MAX 2048
#define _POSIX2_LINE_MAX 2048

#endif
