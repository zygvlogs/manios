#include "heap.h"
#include <stdint.h>
#include "cpu.h"
#include "memlayout.h"
#include "panic.h"
#include "pmm.h"
#include "vmm.h"

#define HEAP_ALIGN 16
#define MAGIC_USED 0x5A4B5455u /* "ZKTU" */
#define MAGIC_FREE 0x5A4B5446u /* "ZKTF" */

/* Blocks tile the mapped heap [KERNEL_HEAP_START, heap_end) exactly and
 * in address order, so a block's list successor is also its neighbour,
 * and no two free blocks are ever adjacent (kfree coalesces). */
struct block {
	uint32_t magic;
	size_t size; /* payload bytes, a multiple of HEAP_ALIGN */
	struct block *next;
	uint32_t reserved; /* pads the header to HEAP_ALIGN */
};

_Static_assert(sizeof(struct block) % HEAP_ALIGN == 0,
               "heap block header must preserve payload alignment");

static struct block *heap_head;
static uintptr_t heap_end; /* first unmapped byte of the heap region */
static size_t used_bytes;

void heap_init(void)
{
	heap_head = NULL;
	heap_end = KERNEL_HEAP_START;
}

/* Maps enough pages at heap_end for a block with `size` payload bytes
 * and appends them as free space. */
static int heap_grow(size_t size)
{
	size_t bytes = (sizeof(struct block) + size + PAGE_SIZE - 1)
	               & ~(size_t)(PAGE_SIZE - 1);
	if (bytes > KERNEL_HEAP_START + KERNEL_HEAP_MAX_SIZE - heap_end) {
		return -1;
	}

	for (uintptr_t off = 0; off < bytes; off += PAGE_SIZE) {
		uintptr_t frame = pmm_alloc_frame();
		if (frame && vmm_map_page(heap_end + off, frame, VMM_WRITABLE) == 0) {
			continue;
		}
		if (frame) {
			pmm_free_frame(frame);
		}
		while (off) {
			uintptr_t phys;
			off -= PAGE_SIZE;
			vmm_unmap_page(heap_end + off, &phys);
			pmm_free_frame(phys);
		}
		return -1;
	}

	struct block *last = heap_head;
	while (last && last->next) {
		last = last->next;
	}

	if (last && last->magic == MAGIC_FREE) {
		last->size += bytes;
	} else {
		struct block *b = (struct block *)heap_end;
		b->magic = MAGIC_FREE;
		b->size = bytes - sizeof(struct block);
		b->next = NULL;
		b->reserved = 0;
		if (last) {
			last->next = b;
		} else {
			heap_head = b;
		}
	}
	heap_end += bytes;
	return 0;
}

static void *take_first_fit(size_t size)
{
	for (struct block *b = heap_head; b; b = b->next) {
		if (b->magic != MAGIC_FREE || b->size < size) {
			continue;
		}
		if (b->size >= size + sizeof(struct block) + HEAP_ALIGN) {
			struct block *rest = (struct block *)((uintptr_t)(b + 1) + size);
			rest->magic = MAGIC_FREE;
			rest->size = b->size - size - sizeof(struct block);
			rest->next = b->next;
			rest->reserved = 0;
			b->next = rest;
			b->size = size;
		}
		b->magic = MAGIC_USED;
		used_bytes += b->size;
		return b + 1;
	}
	return NULL;
}

/* The heap is shared by all threads and has no lock of its own: on a
 * single CPU, running each operation with interrupts off is the lock. */
void *kmalloc(size_t size)
{
	if (size == 0 || size > KERNEL_HEAP_MAX_SIZE) {
		return NULL;
	}
	size = (size + HEAP_ALIGN - 1) & ~(size_t)(HEAP_ALIGN - 1);

	uint32_t flags = cpu_irq_save();
	void *p = take_first_fit(size);
	if (!p && heap_grow(size) == 0) {
		p = take_first_fit(size);
	}
	cpu_irq_restore(flags);
	return p;
}

void kfree(void *ptr)
{
	if (!ptr) {
		return;
	}

	uint32_t flags = cpu_irq_save();
	uintptr_t p = (uintptr_t)ptr;
	if (p < KERNEL_HEAP_START + sizeof(struct block) || p >= heap_end
	    || (p & (HEAP_ALIGN - 1))) {
		panic("kfree: pointer is not from kmalloc");
	}
	struct block *b = (struct block *)ptr - 1;
	if (b->magic == MAGIC_FREE) {
		panic("kfree: double free");
	}
	if (b->magic != MAGIC_USED) {
		panic("kfree: heap corruption or pointer not from kmalloc");
	}
	b->magic = MAGIC_FREE;
	used_bytes -= b->size;

	for (struct block *c = heap_head; c;) {
		if (c->magic == MAGIC_FREE && c->next && c->next->magic == MAGIC_FREE) {
			c->size += sizeof(struct block) + c->next->size;
			c->next = c->next->next;
		} else {
			c = c->next;
		}
	}
	cpu_irq_restore(flags);
}

size_t heap_used(void)
{
	return used_bytes;
}

int heap_check(void)
{
	uint32_t flags = cpu_irq_save();
	uintptr_t expected = KERNEL_HEAP_START;
	int ok = 0;
	for (struct block *b = heap_head; b; b = b->next) {
		if ((uintptr_t)b != expected
		    || (b->magic != MAGIC_USED && b->magic != MAGIC_FREE)
		    || b->size % HEAP_ALIGN
		    || (b->magic == MAGIC_FREE && b->next && b->next->magic == MAGIC_FREE)) {
			ok = -1;
			break;
		}
		expected = (uintptr_t)(b + 1) + b->size;
	}
	if (ok == 0 && expected != heap_end) {
		ok = -1;
	}
	cpu_irq_restore(flags);
	return ok;
}
