/* Scatter/gather I/O vectors. */
#ifndef MANIOS_SYS_UIO_H
#define MANIOS_SYS_UIO_H

#include <sys/types.h>

struct iovec {
	void *iov_base;
	size_t iov_len;
};

#endif
