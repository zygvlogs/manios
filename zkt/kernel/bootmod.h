#ifndef ZKT_KERNEL_BOOTMOD_H
#define ZKT_KERNEL_BOOTMOD_H

#include <stddef.h>
#include "multiboot.h"

/* Maps the boot loader's modules (their frames were kept from the PMM)
 * and registers each as a read-only device named after it -- the
 * ManiOS boot loader's boot area becomes /dev/bootarea, which the
 * installer copies to a disk (M14). Needs the VMM and the heap. */
void bootmod_init(const struct boot_module *mods, size_t count);

#endif
