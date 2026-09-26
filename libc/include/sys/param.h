/* System parameters BSD sources use. */
#ifndef MANIOS_SYS_PARAM_H
#define MANIOS_SYS_PARAM_H

#include <limits.h>
#include <sys/types.h>

#define MAXPATHLEN PATH_MAX
#define MAXBSIZE 65536
#define DEV_BSIZE 512

/* Aligning a pointer for any object (fts.c). */
#define ALIGNBYTES (sizeof(long) - 1)
#define ALIGN(p) (((unsigned long)(p) + ALIGNBYTES) & ~ALIGNBYTES)

#endif
