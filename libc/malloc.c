/* malloc over sbrk(): a first-fit free list kept in address order, so
 * freeing coalesces with both neighbours. Headers are 16 bytes, keeping
 * every block 16-byte aligned (sbrk's heap starts page-aligned). */
#include <stdlib.h>
#include <errno.h>
#include <manios.h>
#include <stdint.h>
#include <string.h>

#define ALIGN 16u
#define MAGIC_USED 0x4D414C55u /* "MALU" */
#define MAGIC_FREE 0x4D414C46u /* "MALF" */
#define GROW_MIN (16u * 1024)

struct block {
	size_t size; /* whole block, header included; a multiple of ALIGN */
	uint32_t magic;
	struct block *next; /* free list only */
	uint32_t pad;
};

_Static_assert(sizeof(struct block) == ALIGN, "malloc header must keep alignment");

static struct block *free_list; /* address order */

/* Inserts b into the free list, merging it with adjacent free blocks. */
static void insert_free(struct block *b)
{
	b->magic = MAGIC_FREE;
	struct block *prev = NULL, *cur = free_list;
	while (cur && cur < b) {
		prev = cur;
		cur = cur->next;
	}
	b->next = cur;
	if (cur && (char *)b + b->size == (char *)cur) {
		b->size += cur->size;
		b->next = cur->next;
	}
	if (prev && (char *)prev + prev->size == (char *)b) {
		prev->size += b->size;
		prev->next = b->next;
	} else if (prev) {
		prev->next = b;
	} else {
		free_list = b;
	}
}

static int grow(size_t need)
{
	size_t bytes = need < GROW_MIN ? GROW_MIN : need;
	if (bytes > (size_t)INTPTR_MAX) {
		return -1;
	}
	struct block *b = sbrk((intptr_t)bytes);
	if (b == (void *)-1) {
		return -1;
	}
	b->size = bytes;
	insert_free(b);
	return 0;
}

void *malloc(size_t size)
{
	if (size == 0 || size > (size_t)-1 / 2) {
		if (size) {
			errno = ENOMEM;
		}
		return NULL;
	}
	size_t need = (size + sizeof(struct block) + ALIGN - 1) & ~(size_t)(ALIGN - 1);
	for (int attempt = 0; attempt < 2; attempt++) {
		struct block *prev = NULL;
		for (struct block *b = free_list; b; prev = b, b = b->next) {
			if (b->size < need) {
				continue;
			}
			if (b->size - need >= 2 * sizeof(struct block)) {
				struct block *rest = (struct block *)((char *)b + need);
				rest->size = b->size - need;
				rest->magic = MAGIC_FREE;
				rest->next = b->next;
				b->size = need;
				b->next = rest;
			}
			if (prev) {
				prev->next = b->next;
			} else {
				free_list = b->next;
			}
			b->magic = MAGIC_USED;
			b->next = NULL;
			return b + 1;
		}
		if (attempt == 0 && grow(need) != 0) {
			break;
		}
	}
	errno = ENOMEM;
	return NULL;
}

static struct block *header(void *ptr)
{
	struct block *b = (struct block *)ptr - 1;
	if (((uintptr_t)ptr & (ALIGN - 1)) || b->magic != MAGIC_USED) {
		abort(); /* not from malloc, or freed twice */
	}
	return b;
}

void free(void *ptr)
{
	if (ptr) {
		insert_free(header(ptr));
	}
}

void *calloc(size_t count, size_t size)
{
	if (size && count > (size_t)-1 / size) {
		errno = ENOMEM;
		return NULL;
	}
	void *p = malloc(count * size);
	if (p) {
		memset(p, 0, count * size);
	}
	return p;
}

void *realloc(void *ptr, size_t size)
{
	if (!ptr) {
		return malloc(size);
	}
	if (size == 0) {
		free(ptr);
		return NULL;
	}
	struct block *b = header(ptr);
	size_t have = b->size - sizeof(struct block);
	if (have >= size) {
		return ptr;
	}
	void *p = malloc(size);
	if (p) {
		memcpy(p, ptr, have);
		free(ptr);
	}
	return p;
}
