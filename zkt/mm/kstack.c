#include "kstack.h"
#include <stdbool.h>
#include <stddef.h>
#include "cpu.h"
#include "panic.h"
#include "pmm.h"
#include "vmm.h"

/* Each slot is a guard page (never mapped) followed by the stack, so an
 * overflow faults instead of running into the stack or data below. */
#define SLOT_SIZE ((KSTACK_PAGES + 1) * PAGE_SIZE)
#define SLOT_COUNT (KERNEL_STACKS_SIZE / SLOT_SIZE)

static bool slot_used[SLOT_COUNT];

static uintptr_t slot_stack_base(size_t slot)
{
	return KERNEL_STACKS_START + slot * SLOT_SIZE + PAGE_SIZE;
}

static void unmap_and_free(uintptr_t base, size_t pages)
{
	for (size_t i = 0; i < pages; i++) {
		uintptr_t phys;
		if (vmm_unmap_page(base + i * PAGE_SIZE, &phys) == 0) {
			pmm_free_frame(phys);
		}
	}
}

static bool map_stack(uintptr_t base)
{
	for (size_t i = 0; i < KSTACK_PAGES; i++) {
		uintptr_t frame = pmm_alloc_frame();
		if (frame && vmm_map_page(base + i * PAGE_SIZE, frame, VMM_WRITABLE) == 0) {
			continue;
		}
		if (frame) {
			pmm_free_frame(frame);
		}
		unmap_and_free(base, i);
		return false;
	}
	return true;
}

uintptr_t kstack_alloc(void)
{
	uintptr_t top = 0;
	uint32_t flags = cpu_irq_save();

	for (size_t slot = 0; slot < SLOT_COUNT; slot++) {
		if (slot_used[slot]) {
			continue;
		}
		if (map_stack(slot_stack_base(slot))) {
			slot_used[slot] = true;
			top = slot_stack_base(slot) + KSTACK_SIZE;
		}
		break;
	}

	cpu_irq_restore(flags);
	return top;
}

void kstack_free(uintptr_t top)
{
	uintptr_t offset = top - KSTACK_SIZE - PAGE_SIZE - KERNEL_STACKS_START;
	size_t slot = offset / SLOT_SIZE;

	if (top < KERNEL_STACKS_START + SLOT_SIZE || offset % SLOT_SIZE
	    || slot >= SLOT_COUNT || !slot_used[slot]) {
		panic("kstack_free: not the top of a live kernel stack");
	}

	uint32_t flags = cpu_irq_save();
	unmap_and_free(slot_stack_base(slot), KSTACK_PAGES);
	slot_used[slot] = false;
	cpu_irq_restore(flags);
}
