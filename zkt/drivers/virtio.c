/* Legacy virtio over PCI (virtio.h), written from the Virtio PCI Card
 * Specification 0.9.5 and OASIS Virtio 1.x's "Legacy Interface"
 * sections. */
#include "virtio.h"
#include "dma.h"
#include "io.h"
#include "memlayout.h"

#define barrier() __asm__ volatile("" ::: "memory")

uint16_t virtio_start(const struct pci_device *pci)
{
	uint16_t io = (uint16_t)pci_bar_io(pci, 0);
	if (!io) {
		return 0;
	}
	pci_enable(pci);
	outb(io + VIRTIO_STATUS, 0); /* reset */
	outb(io + VIRTIO_STATUS, VIRTIO_STATUS_ACK);
	outb(io + VIRTIO_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);
	return io;
}

uint32_t virtio_features(uint16_t io, uint32_t wanted)
{
	uint32_t features = inl(io + VIRTIO_DEVICE_FEATURES) & wanted;
	outl(io + VIRTIO_GUEST_FEATURES, features);
	return features;
}

/* The legacy layout: descriptors, then the available ring, then -- at
 * the next page -- the used ring. */
static uint32_t ring_bytes(uint16_t size)
{
	uint32_t first = 16u * size + 2u * (3u + size);
	first = (first + PAGE_SIZE - 1) & ~(uint32_t)(PAGE_SIZE - 1);
	return first + 2u * 3u + 8u * size;
}

bool virtio_queue(uint16_t io, uint16_t index, struct virtq *q)
{
	outw(io + VIRTIO_QUEUE_SELECT, index);
	uint16_t size = inw(io + VIRTIO_QUEUE_SIZE);
	if (!size) {
		return false;
	}
	uintptr_t phys;
	uint8_t *mem = dma_alloc(ring_bytes(size), false, &phys);
	if (!mem) {
		return false;
	}
	uint32_t used_at = (16u * size + 2u * (3u + size) + PAGE_SIZE - 1) & ~(uint32_t)(PAGE_SIZE - 1);
	q->size = size;
	q->desc = (volatile struct vring_desc *)mem;
	q->avail = (volatile uint16_t *)(mem + 16u * size);
	q->used = (volatile uint16_t *)(mem + used_at);
	q->last_used = 0;
	outl(io + VIRTIO_QUEUE_PFN, (uint32_t)(phys / PAGE_SIZE));
	return true;
}

void virtio_ready(uint16_t io)
{
	outb(io + VIRTIO_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
}

void virtq_submit(uint16_t io, uint16_t index, struct virtq *q, uint16_t head)
{
	uint16_t idx = q->avail[1];
	q->avail[2 + idx % q->size] = head;
	barrier();
	q->avail[1] = (uint16_t)(idx + 1);
	barrier();
	outw(io + VIRTIO_QUEUE_NOTIFY, index);
}

bool virtq_used(struct virtq *q, struct vring_used_elem *out)
{
	if (q->used[1] == q->last_used) {
		return false;
	}
	barrier();
	*out = virtq_used_ring(q)[q->last_used % q->size];
	q->last_used++;
	return true;
}
