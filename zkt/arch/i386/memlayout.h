/* i386 kernel memory layout. Generic code includes this by name (via the
 * arch include path) rather than by path, so another architecture can
 * supply its own memlayout.h without touching zkt/mm. Also included by
 * boot.S, hence plain literals and the __ASSEMBLER__ guard. */
#ifndef ZKT_ARCH_I386_MEMLAYOUT_H
#define ZKT_ARCH_I386_MEMLAYOUT_H

#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

/* Must match KERNEL_VIRT_BASE in linker.ld. */
#define KERNEL_VIRT_BASE 0xC0000000

/* boot.S maps physical [0, BOOT_MAPPED_PHYS_LIMIT) at KERNEL_VIRT_BASE
 * with a single page table; anything the kernel touches before the VMM
 * is up (the image itself, the PMM bitmap, Multiboot data) must lie
 * below this limit. */
#define BOOT_MAPPED_PHYS_LIMIT 0x00400000

#define KERNEL_HEAP_START 0xD0000000
#define KERNEL_HEAP_MAX_SIZE 0x10000000

/* One otherwise-unused kernel page for the boot-time VMM self-test. */
#define KERNEL_SELFTEST_VIRT 0xE0000000

/* The last page directory entry maps the page directory itself, which
 * exposes every page table at a fixed virtual address. */
#define RECURSIVE_PD_INDEX 1023
#define RECURSIVE_PT_BASE 0xFFC00000
#define RECURSIVE_PD_ADDR 0xFFFFF000

#ifndef __ASSEMBLER__
#include <stdint.h>

#define P2V(pa) ((void *)((uintptr_t)(pa) + KERNEL_VIRT_BASE))
#define V2P(va) ((uintptr_t)(va) - KERNEL_VIRT_BASE)

/* linker.ld: first byte past the kernel image, .bss included (virtual). */
extern char _kernel_end[];
#endif

#endif
