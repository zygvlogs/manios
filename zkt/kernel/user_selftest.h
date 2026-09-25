#ifndef ZKT_KERNEL_USER_SELFTEST_H
#define ZKT_KERNEL_USER_SELFTEST_H

/* Runs user programs from the boot archive: /bin/hello, then the system
 * call conformance test /boot/test/utest (which exercises faults,
 * address space isolation and namespaces), and checks that nothing
 * leaked. Panics on failure. Needs fs_init(). */
void user_selftest(void);

#endif
