#include "mm_selftest.h"
#include <stdint.h>
#include "heap.h"
#include "memlayout.h"
#include "panic.h"
#include "pmm.h"
#include "vmm.h"

static void expect(int ok, const char *failure)
{
	if (!ok) {
		panic(failure);
	}
}

static void pmm_selftest(void)
{
	size_t before = pmm_free_frames();
	uintptr_t a = pmm_alloc_frame();
	uintptr_t b = pmm_alloc_frame();

	expect(a && b && a != b, "selftest: pmm returned a null or duplicate frame");
	expect(!(a & (PAGE_SIZE - 1)) && !(b & (PAGE_SIZE - 1)),
	       "selftest: pmm returned a misaligned frame");
	expect(pmm_free_frames() == before - 2, "selftest: pmm free count wrong after alloc");

	pmm_free_frame(a);
	pmm_free_frame(b);
	expect(pmm_free_frames() == before, "selftest: pmm free count wrong after free");

	uintptr_t c = pmm_alloc_frame();
	expect(c == a, "selftest: pmm did not reuse the lowest freed frame");
	pmm_free_frame(c);
}

static void vmm_selftest(void)
{
	uintptr_t virt = KERNEL_SELFTEST_VIRT;
	uintptr_t frame = pmm_alloc_frame();
	uintptr_t phys;

	expect(frame != 0, "selftest: no frame for the vmm test");
	expect(vmm_translate(virt, &phys) != 0, "selftest: vmm test page already mapped");
	expect(vmm_map_page(virt, frame, VMM_WRITABLE) == 0, "selftest: vmm_map_page failed");
	expect(vmm_map_page(virt, frame, VMM_WRITABLE) != 0,
	       "selftest: vmm_map_page accepted an already-mapped page");

	volatile uint32_t *word = (volatile uint32_t *)virt;
	*word = 0x5A4B5421u;
	expect(*word == 0x5A4B5421u, "selftest: write through new mapping did not stick");

	expect(vmm_translate(virt + 0x123, &phys) == 0 && phys == frame + 0x123,
	       "selftest: vmm_translate returned the wrong physical address");
	expect(vmm_translate(virt + PAGE_SIZE, &phys) != 0,
	       "selftest: fresh page table was not zeroed");

	expect(vmm_unmap_page(virt, &phys) == 0 && phys == frame,
	       "selftest: vmm_unmap_page failed");
	expect(vmm_translate(virt, &phys) != 0, "selftest: page still mapped after unmap");
	pmm_free_frame(frame);
}

static int aligned16(const void *p)
{
	return !((uintptr_t)p & 15);
}

static void heap_selftest(void)
{
	expect(kmalloc(0) == NULL, "selftest: kmalloc(0) did not return NULL");

	uint8_t *a = kmalloc(24);
	uint8_t *b = kmalloc(100);
	uint8_t *c = kmalloc(1);
	expect(a && b && c, "selftest: kmalloc returned NULL");
	expect(aligned16(a) && aligned16(b) && aligned16(c),
	       "selftest: kmalloc returned a misaligned pointer");

	for (int i = 0; i < 24; i++) {
		a[i] = 0xA5;
	}
	for (int i = 0; i < 100; i++) {
		b[i] = 0x5A;
	}
	c[0] = 0x3C;
	expect(a[23] == 0xA5 && b[0] == 0x5A && b[99] == 0x5A && c[0] == 0x3C,
	       "selftest: kmalloc blocks overlap");
	expect(heap_check() == 0, "selftest: heap inconsistent after allocation");

	kfree(b);
	uint8_t *b2 = kmalloc(100);
	expect(b2 == b, "selftest: kmalloc did not reuse a freed block");

	kfree(a);
	kfree(b2);
	kfree(c);
	expect(heap_check() == 0, "selftest: heap inconsistent after free");

	/* Larger than everything mapped so far: forces the heap to grow. */
	size_t big_size = 3 * PAGE_SIZE + 5;
	uint8_t *big = kmalloc(big_size);
	expect(big != NULL, "selftest: kmalloc could not grow the heap");
	for (size_t i = 0; i < big_size; i++) {
		big[i] = (uint8_t)i;
	}
	expect(big[big_size - 1] == (uint8_t)(big_size - 1),
	       "selftest: grown heap memory is not usable");
	kfree(big);
	expect(heap_check() == 0, "selftest: heap inconsistent after growth");
}

void mm_selftest(void)
{
	pmm_selftest();
	vmm_selftest();
	heap_selftest();
}
