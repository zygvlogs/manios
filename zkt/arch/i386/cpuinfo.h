#ifndef ZKT_ARCH_I386_CPUINFO_H
#define ZKT_ARCH_I386_CPUINFO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Describes the CPU in one line: its brand string where it has one
 * ("QEMU Virtual CPU version 2.5+"), else CPUID's vendor, family and
 * model, else "486" or "386" for CPUs without CPUID. */
void cpu_describe(char *buf, size_t size);

#endif
