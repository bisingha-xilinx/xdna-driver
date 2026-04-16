/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#ifndef _AMDXDNA_IPC_DOORBELL_H_
#define _AMDXDNA_IPC_DOORBELL_H_

/*
 * Lock-free SPSC doorbell ring for lightweight HSA command submission
 * notifications between HW Domains and management processor via shared memory.
 *
 * Producer (HW Domain) owns head; consumer (management processor) owns tail.
 * Each data entry is a u32 (e.g., hw_context_id).
 *
 * Barrier discipline:
 *   Producer: writel(data) -> dma_wmb() -> smp_store_release(head)
 *   Consumer: smp_load_acquire(head) -> dma_rmb() -> readl(data)
 *
 * Notification suppression:
 *   Consumer writes notify_idx to request a kick when head reaches that value.
 *   Producer checks: if (new_head == notify_idx) then kick IPI.
 *
 * Hot-path MMIO budget: 1 peer-index read + 1 data write + 1 index write
 *   (mask and own index are cached locally in struct amdxdna_ipc_ring)
 */

#include "amdxdna_ipc.h"

/* Shared-memory layout -- maps directly over the ioremap'd shared memory region */
struct amdxdna_ipc_db_ring {
	__le32	head;		/* producer write position (free-running) */
	__le32	tail;		/* consumer read position (free-running) */
	__le32	ring_mask;	/* N-1, written once at init, read once by peer */
	__le32	notify_idx;	/* event-index: kick peer when head crosses this */
	__le32	data[];		/* ring entries (u32 each) */
};

/*
 * Initialize doorbell ring shared memory and local context.
 * num_entries must be a power of 2. Returns 0 or -EINVAL.
 */
static inline int amdxdna_ipc_db_init(struct amdxdna_ipc_ring *ring,
				       void __iomem *base, u32 num_entries)
{
	struct amdxdna_ipc_db_ring __iomem *hdr = base;

	if (!num_entries || (num_entries & (num_entries - 1)))
		return -EINVAL;

	ring->base = base;
	ring->mask = num_entries - 1;
	ring->cached_idx = 0;

	writel(0, &hdr->head);
	writel(0, &hdr->tail);
	writel(num_entries - 1, &hdr->ring_mask);
	writel(0, &hdr->notify_idx);

	return 0;
}

/*
 * Attach to an already-initialized doorbell ring (peer did init).
 * Reads ring_mask from shared memory once and caches it locally.
 */
static inline void amdxdna_ipc_db_attach(struct amdxdna_ipc_ring *ring,
					  void __iomem *base)
{
	struct amdxdna_ipc_db_ring __iomem *hdr = base;

	ring->base = base;
	ring->mask = readl(&hdr->ring_mask);
	ring->cached_idx = 0;
}

/* Number of pending entries (producer perspective) */
static inline u32 amdxdna_ipc_db_count(struct amdxdna_ipc_ring *ring)
{
	struct amdxdna_ipc_db_ring __iomem *hdr = ring->base;

	return ring->cached_idx - readl(&hdr->tail);
}

/* Enqueue one value (producer). Returns 0 or -ENOSPC. */
static inline int amdxdna_ipc_db_produce(struct amdxdna_ipc_ring *ring,
					  u32 val)
{
	struct amdxdna_ipc_db_ring __iomem *hdr = ring->base;
	u32 head = ring->cached_idx;
	u32 tail = smp_load_acquire(&hdr->tail);

	if (head - tail > ring->mask)
		return -ENOSPC;

	writel(val, &hdr->data[head & ring->mask]);
	dma_wmb();
	ring->cached_idx = head + 1;
	smp_store_release(&hdr->head, head + 1);
	return 0;
}

/*
 * Enqueue multiple values (producer). One barrier + one index publish.
 * Returns 0 or -ENOSPC (none enqueued on failure).
 */
static inline int amdxdna_ipc_db_produce_batch(struct amdxdna_ipc_ring *ring,
					        const u32 *vals, u32 count)
{
	struct amdxdna_ipc_db_ring __iomem *hdr = ring->base;
	u32 head = ring->cached_idx;
	u32 tail = smp_load_acquire(&hdr->tail);
	u32 i;

	if (head - tail + count > ring->mask + 1)
		return -ENOSPC;

	for (i = 0; i < count; i++)
		writel(vals[i], &hdr->data[(head + i) & ring->mask]);

	dma_wmb();
	ring->cached_idx = head + count;
	smp_store_release(&hdr->head, head + count);
	return 0;
}

/* Dequeue one value (consumer). Returns the value (>=0) or -ENODATA. */
static inline int amdxdna_ipc_db_consume(struct amdxdna_ipc_ring *ring)
{
	struct amdxdna_ipc_db_ring __iomem *hdr = ring->base;
	u32 head = smp_load_acquire(&hdr->head);
	u32 tail = ring->cached_idx;
	u32 val;

	if (head == tail)
		return -ENODATA;

	dma_rmb();
	val = readl(&hdr->data[tail & ring->mask]);
	ring->cached_idx = tail + 1;
	smp_store_release(&hdr->tail, tail + 1);
	return (int)val;
}

/*
 * Dequeue up to max_count values (consumer). One barrier + one tail publish.
 * Returns number of entries consumed (0 if empty).
 */
static inline u32 amdxdna_ipc_db_consume_batch(struct amdxdna_ipc_ring *ring,
						u32 *out, u32 max_count)
{
	struct amdxdna_ipc_db_ring __iomem *hdr = ring->base;
	u32 head = smp_load_acquire(&hdr->head);
	u32 tail = ring->cached_idx;
	u32 avail = head - tail;
	u32 count, i;

	if (!avail)
		return 0;

	count = min(avail, max_count);
	dma_rmb();
	for (i = 0; i < count; i++)
		out[i] = readl(&hdr->data[(tail + i) & ring->mask]);

	ring->cached_idx = tail + count;
	smp_store_release(&hdr->tail, tail + count);
	return count;
}

/*
 * Check whether the producer should kick the peer (event-index pattern).
 * Returns true if cached_idx (new head) has crossed the consumer's
 * notify_idx threshold. Caller decides whether to trigger IPI.
 */
static inline bool amdxdna_ipc_db_need_kick(struct amdxdna_ipc_ring *ring)
{
	struct amdxdna_ipc_db_ring __iomem *hdr = ring->base;

	return ring->cached_idx == readl(&hdr->notify_idx);
}

/*
 * Set the notification threshold (consumer side).
 * Tells producer: "kick me when your head reaches idx."
 */
static inline void amdxdna_ipc_db_set_notify(struct amdxdna_ipc_ring *ring,
					      u32 idx)
{
	struct amdxdna_ipc_db_ring __iomem *hdr = ring->base;

	smp_store_release(&hdr->notify_idx, idx);
}

#endif /* _AMDXDNA_IPC_DOORBELL_H_ */
