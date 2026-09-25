#ifndef ZKT_KERNEL_SYSCALL_H
#define ZKT_KERNEL_SYSCALL_H

#include <stdint.h>

/* Runs system call `num` (zkt_abi.h) for the calling process; returns
 * the value for EAX. The arch layer extracts the arguments. */
long syscall_dispatch(uint32_t num, uint32_t a0, uint32_t a1, uint32_t a2,
                      uint32_t a3, uint32_t a4);

#endif
