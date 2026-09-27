/* Virtio devices over PCI, in the "legacy" interface (virtio 0.9.5 and
 * the transitional devices of virtio 1.x, which QEMU, VirtualBox and
 * other hypervisors offer): registers in I/O BAR 0, virtqueues in
 * guest memory at a page number the driver writes. Shared by the
 * virtio drivers (virtio_blk.c, virtio_net.c). */
#ifndef ZKT_DRIVERS_VIRTIO_H
#define ZKT_DRIVERS_VIRTIO_H

#include <stdbool.h>
#include <stdint.h>
#include "pci.h"

#define VIRTIO_VENDOR 0x1AF4
/* Legacy registers, from BAR 0. */
#define VIRTIO_DEVICE_FEATURES 0x00
#define VIRTIO_GUEST_FEATURES  0x04
#define VIRTIO_QUEUE_PFN       0x08
#define VIRTIO_QUEUE_SIZE      0x0C
#define VIRTIO_QUEUE_SELECT    0x0E
#define VIRTIO_QUEUE_NOTIFY    0x10
#define VIRTIO_STATUS          0x12
#define VIRTIO_ISR             0x13
#define VIRTIO_CONFIG          0x14 /* the device's own, without MSI-X */

#define VIRTIO_STATUS_ACK       1
#define VIRTIO_STATUS_DRIVER    2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FAILED    128

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2 /* the device writes this buffer */
#define VRING_AVAIL_F_NO_INTERRUPT 1

struct vring_desc {
	uint64_t addr;
	uint32_t len;
	uint16_t flags, next;
};

struct vring_used_elem {
	uint32_t id, len;
};

struct virtq {
	uint16_t size;
	volatile struct vring_desc *desc;
	volatile uint16_t *avail; /* flags, idx, ring[size], used_event */
	volatile uint16_t *used;  /* flags, idx, then ring[size] of vring_used_elem */
	uint16_t last_used;       /* the used entries seen so far */
};

/* Resets the device and acknowledges it: its I/O base, or 0 if BAR 0
 * isn't an I/O BAR. Leaves the device ready for features and queues. */
uint16_t virtio_start(const struct pci_device *pci);
/* Takes the features the device has of `wanted`: the ones in use. */
uint32_t virtio_features(uint16_t io, uint32_t wanted);
/* Sets queue `index` up in new memory (below 16 MiB, zeroed). */
bool virtio_queue(uint16_t io, uint16_t index, struct virtq *q);
/* The driver is ready: the device starts. */
void virtio_ready(uint16_t io);
/* Makes descriptor chain `head` available and tells the device. */
void virtq_submit(uint16_t io, uint16_t index, struct virtq *q, uint16_t head);
/* The next used element, if the device has returned one. */
bool virtq_used(struct virtq *q, struct vring_used_elem *out);

static inline volatile struct vring_used_elem *virtq_used_ring(struct virtq *q)
{
	return (volatile struct vring_used_elem *)(q->used + 2);
}

#endif
