#ifndef MANIOS_ERRNO_H
#define MANIOS_ERRNO_H

#include <zkt_abi.h> /* the error numbers themselves */

/* Set by a failing library call; never cleared by a successful one.
 * One per process: ZKT processes have a single thread. */
extern int errno;

#endif
