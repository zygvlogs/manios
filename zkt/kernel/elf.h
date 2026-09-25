#ifndef ZKT_KERNEL_ELF_H
#define ZKT_KERNEL_ELF_H

#include <stddef.h>
#include <stdint.h>

/* Lowest address a program may occupy: page 0 stays unmapped, so NULL
 * dereferences fault. */
#define USER_IMAGE_MIN 0x00001000

/* Loads a static ELF32 i386 executable into the *active* address space:
 * maps and zeroes pages for each PT_LOAD segment, copies file contents,
 * makes segments without PF_W read-only, and stores the entry point. Every field is treated as untrusted.
 * Returns 0 or -ENOEXEC (malformed or unsupported), -ENOMEM. On error,
 * pages already mapped stay mapped; the caller discards the whole
 * address space. */
int elf_load(const uint8_t *image, size_t size, uintptr_t *entry);

#endif
