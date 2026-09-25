#ifndef ZKT_KERNEL_USER_SELFTEST_H
#define ZKT_KERNEL_USER_SELFTEST_H

/* Runs user programs from the boot archive: /bin/hello, then the system
 * call conformance test /boot/test/utest (which exercises faults,
 * address space isolation and namespaces), and checks that nothing
 * leaked. Panics on failure. Needs fs_init(). */
void user_selftest(void);

/* The same for the C library's conformance test, /boot/test/ctest, and
 * the 2D graphics library's, /boot/test/gtest. */
void libc_selftest(void);
void gfx_selftest(void);

/* The userspace file server test, /boot/test/ztest: a program serving
 * files over a pipe mounted with SYS_MOUNTFD (M12). */
void channel_selftest(void);

#endif
