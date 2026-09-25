#include "pmm.h"
#include "cpu.h"
#include "kstring.h"
#include "memlayout.h"
#include "panic.h"

/* Without PAE, physical memory at or above 4 GiB is unreachable. */
#define PHYS_ADDR_LIMIT 0x100000000ULL
#define BITS_PER_WORD 32

static uint32_t *bitmap;   /* bit set = frame in use, reserved, or absent */
static size_t frame_count; /* frames covered by the bitmap */
static size_t word_count;
static size_t search_hint; /* every bitmap word below this index is full */
static size_t free_count;
static size_t usable_count;

static void mark_used(size_t frame)
{
	bitmap[frame / BITS_PER_WORD] |= 1u << (frame % BITS_PER_WORD);
}

static void mark_free(size_t frame)
{
	bitmap[frame / BITS_PER_WORD] &= ~(1u << (frame % BITS_PER_WORD));
}

static bool is_used(size_t frame)
{
	return bitmap[frame / BITS_PER_WORD] & (1u << (frame % BITS_PER_WORD));
}

static uint64_t region_end(const struct mem_region *r)
{
	uint64_t end = r->base + r->length;
	return end > PHYS_ADDR_LIMIT ? PHYS_ADDR_LIMIT : end;
}

static size_t count_free(void)
{
	size_t n = 0;
	for (size_t f = 0; f < frame_count; f++) {
		if (!is_used(f)) {
			n++;
		}
	}
	return n;
}

void pmm_init(const struct mem_region *regions, size_t count)
{
	uint64_t top = 0;
	for (size_t i = 0; i < count; i++) {
		if (regions[i].usable && regions[i].base < PHYS_ADDR_LIMIT
		    && region_end(&regions[i]) > top) {
			top = region_end(&regions[i]);
		}
	}
	if (top == 0) {
		panic("pmm: boot loader reported no usable memory");
	}

	frame_count = (size_t)(top >> PAGE_SHIFT);
	word_count = (frame_count + BITS_PER_WORD - 1) / BITS_PER_WORD;

	/* The bitmap sits right after the kernel image, inside the boot
	 * mapping, since no allocator exists yet to place it elsewhere. */
	uintptr_t bitmap_start = (uintptr_t)_kernel_end;
	uintptr_t bitmap_end = (bitmap_start + word_count * sizeof(uint32_t) + PAGE_SIZE - 1)
	                       & ~(uintptr_t)(PAGE_SIZE - 1);
	if (V2P(bitmap_end) > BOOT_MAPPED_PHYS_LIMIT) {
		panic("pmm: frame bitmap does not fit in the boot mapping");
	}
	bitmap = (uint32_t *)bitmap_start;
	memset(bitmap, 0xFF, word_count * sizeof(uint32_t));

	/* Usable regions round inward and reserved regions round outward, so
	 * a frame straddling both (overlapping BIOS map entries) stays used. */
	for (size_t i = 0; i < count; i++) {
		if (!regions[i].usable || regions[i].base >= PHYS_ADDR_LIMIT) {
			continue;
		}
		size_t first = (size_t)((regions[i].base + PAGE_SIZE - 1) >> PAGE_SHIFT);
		size_t end = (size_t)(region_end(&regions[i]) >> PAGE_SHIFT);
		for (size_t f = first; f < end && f < frame_count; f++) {
			mark_free(f);
		}
	}
	for (size_t i = 0; i < count; i++) {
		if (regions[i].usable || regions[i].base >= PHYS_ADDR_LIMIT) {
			continue;
		}
		size_t first = (size_t)(regions[i].base >> PAGE_SHIFT);
		size_t end = (size_t)((region_end(&regions[i]) + PAGE_SIZE - 1) >> PAGE_SHIFT);
		for (size_t f = first; f < end && f < frame_count; f++) {
			mark_used(f);
		}
	}
	usable_count = count_free();

	for (size_t f = 0; f < (V2P(bitmap_end) >> PAGE_SHIFT) && f < frame_count; f++) {
		mark_used(f);
	}
	free_count = count_free();
	search_hint = 0;
}

/* Interrupts off is the PMM's lock (single CPU), as for the heap. */
uintptr_t pmm_alloc_frame(void)
{
	uintptr_t phys = 0;
	uint32_t flags = cpu_irq_save();
	size_t w = search_hint;
	while (w < word_count && bitmap[w] == 0xFFFFFFFFu) {
		w++;
	}
	search_hint = w;
	if (w < word_count) {
		size_t frame = w * BITS_PER_WORD + (size_t)__builtin_ctz(~bitmap[w]);
		mark_used(frame);
		free_count--;
		phys = (uintptr_t)frame << PAGE_SHIFT;
	}
	cpu_irq_restore(flags);
	return phys;
}

void pmm_free_frame(uintptr_t phys)
{
	size_t frame = phys >> PAGE_SHIFT;

	if (phys & (PAGE_SIZE - 1)) {
		panic("pmm_free_frame: address is not page-aligned");
	}
	if (frame >= frame_count) {
		panic("pmm_free_frame: address is beyond physical memory");
	}

	uint32_t flags = cpu_irq_save();
	if (!is_used(frame)) {
		panic("pmm_free_frame: frame is already free (double free)");
	}
	mark_free(frame);
	free_count++;
	if (frame / BITS_PER_WORD < search_hint) {
		search_hint = frame / BITS_PER_WORD;
	}
	cpu_irq_restore(flags);
}

size_t pmm_free_frames(void)
{
	return free_count;
}

size_t pmm_usable_frames(void)
{
	return usable_count;
}
