#ifndef MANIOS_ERRNO_H
#define MANIOS_ERRNO_H

#include <zkt_abi.h> /* the error numbers themselves */

/* Numbers only the library reports; the kernel never returns them. */
#define EINTR   4 /* never: ManiOS delivers no signals */
#define EAGAIN 11 /* never yet: nothing is non-blocking */
#define ENOTTY 25 /* not a terminal: ioctl() */
#define EILSEQ 84 /* an illegal byte sequence */

/* Set by a failing library call; never cleared by a successful one.
 * One per process: ZKT processes have a single thread. */
extern int errno;

#endif
