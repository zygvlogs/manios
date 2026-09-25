#ifndef ZKT_MM_MM_SELFTEST_H
#define ZKT_MM_MM_SELFTEST_H

/* Exercises the PMM, VMM and heap at boot; panics naming the first
 * failed check. Runs every boot so real old hardware reports memory
 * problems immediately, and so tests/boot_smoke_test.sh can gate on it. */
void mm_selftest(void);

#endif
