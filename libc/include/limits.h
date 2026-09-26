/* The compiler's <limits.h> (the integer types), and ManiOS's limits. */
#ifndef MANIOS_LIMITS_H
#define MANIOS_LIMITS_H

#include_next <limits.h>
#include <zkt_abi.h>

#define PATH_MAX (ZKT_PATH_MAX + 1) /* with its NUL */
#define LINE_MAX 2048
#define _POSIX2_LINE_MAX 2048
#define NAME_MAX ZKT_NAME_MAX
#define SSIZE_MAX LONG_MAX
#define NL_TEXTMAX 255           /* a message's length (nl's regex errors) */
#define _POSIX2_RE_DUP_MAX 255   /* the largest n in a regex's {n} */
#define RE_DUP_MAX _POSIX2_RE_DUP_MAX

#endif
