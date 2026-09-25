#ifndef ZKT_NET_NET_SELFTEST_H
#define ZKT_NET_NET_SELFTEST_H

/* Pings 127.0.0.1, then runs a ZRP server exporting /boot and mounts it
 * over the loopback interface in a private namespace: files, listings,
 * errors and a program loaded over ZRP must all match the local tree,
 * and nothing may leak. Needs net_init() and the boot archive. */
void net_selftest(void);

#endif
