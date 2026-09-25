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

/* Two address spaces: their user halves are private, and a kernel page
 * table created while either is active reaches both (paging.c). */
static void address_space_selftest(void)
{
	const uintptr_t user_va = 0x00400000;
	const uintptr_t kernel_va = KERNEL_SELFTEST_VIRT + 0x00400000; /* no page table yet */
	const uint32_t magic = 0x5A4B5441u;
	uintptr_t phys;

	/* The first address space also creates kernel tables that stay. */
	struct address_space *warmup = vmm_as_create();
	expect(warmup != NULL, "selftest: vmm_as_create failed");
	vmm_as_destroy(warmup);
	size_t frames = pmm_free_frames();

	struct address_space *a = vmm_as_create(), *b = vmm_as_create();
	uintptr_t user_frame = pmm_alloc_frame(), kernel_frame = pmm_alloc_frame();
	expect(a && b && user_frame && kernel_frame, "selftest: out of memory in the address space test");
	expect(vmm_translate(kernel_va, &phys) != 0, "selftest: address space test page already mapped");

	vmm_as_activate(a);
	expect(vmm_map_page(user_va, user_frame, VMM_WRITABLE | VMM_USER) == 0,
	       "selftest: mapping a user page failed");
	expect(vmm_map_page(kernel_va, kernel_frame, VMM_WRITABLE | VMM_USER) != 0
	       && vmm_map_page(user_va + PAGE_SIZE, kernel_frame, VMM_WRITABLE) != 0,
	       "selftest: a user mapping in the kernel half, or the reverse, was accepted");
	expect(vmm_map_page(kernel_va, kernel_frame, VMM_WRITABLE) == 0,
	       "selftest: mapping a kernel page from a user address space failed");
	*(volatile uint32_t *)kernel_va = magic;
	expect(vmm_user_range_ok(user_va, PAGE_SIZE, true)
	       && !vmm_user_range_ok(user_va, PAGE_SIZE + 1, false)
	       && !vmm_user_range_ok(kernel_va, 4, false),
	       "selftest: vmm_user_range_ok misjudged a range");

	vmm_as_activate(b);
	expect(vmm_translate(user_va, &phys) != 0 && !vmm_user_range_ok(user_va, 1, false),
	       "selftest: a user page showed up in another address space");
	expect(vmm_translate(kernel_va, &phys) == 0 && *(volatile uint32_t *)kernel_va == magic,
	       "selftest: a new kernel page table did not reach every address space");
	vmm_as_activate(NULL);
	expect(vmm_translate(kernel_va, &phys) == 0 && *(volatile uint32_t *)kernel_va == magic,
	       "selftest: a new kernel page table did not reach the kernel's address space");

	vmm_as_activate(a);
	vmm_as_clear_user(); /* frees user_frame and its page table */
	vmm_as_activate(NULL);
	expect(vmm_unmap_page(kernel_va, &phys) == 0 && phys == kernel_frame,
	       "selftest: unmapping the kernel test page failed");
	pmm_free_frame(kernel_frame);
	vmm_as_destroy(a);
	vmm_as_destroy(b);
	/* All that stays is kernel_va's page table, shared by every space. */
	expect(pmm_free_frames() == frames - 1, "selftest: address spaces leaked frames");
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
	address_space_selftest();
	heap_selftest();
}
