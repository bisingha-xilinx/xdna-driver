/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#ifndef _AMDXDNA_IPC_MGMT_H_
#define _AMDXDNA_IPC_MGMT_H_

/*
 * Lock-free SPSC management buffer-descriptor ring (BDR) for management
 * messages between HW Domains and management processor via shared memory.
 *
 * Separate buffer pool (at buf_base, not contiguous with the ring):
 *   buf[i] = buf_base + i * buf_size
 * Each buffer contains an amdxdna_ipc_msg_hdr followed by payload.
 *
 * Producer (HW Domain) owns head; consumer (management processor) owns tail.
 *
 * Barrier discipline:
 *   Producer: fill buffer -> dma_wmb() -> smp_store_release(head)
 *   Consumer: smp_load_acquire(head) -> dma_rmb() -> read buffer
 *
 * Notification suppression:
 *   Consumer writes notify_idx to request a kick when head reaches that value.
 *   Producer checks: if (new_head == notify_idx) then kick IPI.
 */

#include "amdxdna_ipc.h"

/* Compile-time ring configuration defaults */
#define AMDXDNA_IPC_MGMT_NUM_DESCS	16	/* power of 2 */
#define AMDXDNA_IPC_MGMT_BUF_SIZE	4096

/* Shared-memory layout -- maps directly over the ioremap'd shared memory region */
struct amdxdna_ipc_mgmt_ring {
	__le32	head;		/* producer write position (free-running) */
	__le32	tail;		/* consumer read position (free-running) */
	__le32	ring_mask;	/* N-1, written once at init */
	__le32	buf_size;	/* size of each buffer in the pool */
	__le32	buf_base_lo;	/* buffer pool base address (low 32 bits) */
	__le32	buf_base_hi;	/* buffer pool base address (high 32 bits) */
	__le32	version;	/* protocol version (checked once at init) */
	__le32	notify_idx;	/* event-index for notification suppression */
	__le32	desc[];		/* descriptor offsets into buffer pool */
};

/* Per-message header in each mgmt buffer (16 bytes) */
struct amdxdna_ipc_msg_hdr {
	u32	opcode;		/* message opcode */
	u32	msg_id;		/* request/response correlation ID */
	u32	size;		/* payload size in bytes (excludes this header) */
	u32	flags;		/* bitfield (response, error, etc.) */
};

#define AMDXDNA_IPC_MSG_HDR_SIZE	sizeof(struct amdxdna_ipc_msg_hdr)
#define AMDXDNA_IPC_MSG_FLAG_RESPONSE	BIT(0)
#define AMDXDNA_IPC_MSG_FLAG_ERROR	BIT(1)

/*
 * Initialize management BDR shared memory and local ring context.
 * num_descs must be a power of 2. Returns 0 or -EINVAL.
 */
static inline int amdxdna_ipc_mgmt_init(struct amdxdna_ipc_ring *ring,
					 void __iomem *base, u32 num_descs,
					 u32 buf_size, u32 buf_base_lo,
					 u32 buf_base_hi, u32 version)
{
	struct amdxdna_ipc_mgmt_ring __iomem *hdr = base;
	u32 i;

	if (!num_descs || (num_descs & (num_descs - 1)))
		return -EINVAL;

	ring->base = base;
	ring->mask = num_descs - 1;
	ring->cached_idx = 0;

	writel(0, &hdr->head);
	writel(0, &hdr->tail);
	writel(num_descs - 1, &hdr->ring_mask);
	writel(buf_size, &hdr->buf_size);
	writel(buf_base_lo, &hdr->buf_base_lo);
	writel(buf_base_hi, &hdr->buf_base_hi);
	writel(version, &hdr->version);
	writel(0, &hdr->notify_idx);

	for (i = 0; i < num_descs; i++)
		writel(i * buf_size, &hdr->desc[i]);

	return 0;
}

/*
 * Attach to an already-initialized management BDR ring (peer did init).
 * Reads ring_mask from shared memory once and caches it locally.
 */
static inline void amdxdna_ipc_mgmt_attach(struct amdxdna_ipc_ring *ring,
					    void __iomem *base)
{
	struct amdxdna_ipc_mgmt_ring __iomem *hdr = base;

	ring->base = base;
	ring->mask = readl(&hdr->ring_mask);
	ring->cached_idx = 0;
}

/* Read protocol version from ring header (one-time check) */
static inline u32 amdxdna_ipc_mgmt_get_version(struct amdxdna_ipc_ring *ring)
{
	struct amdxdna_ipc_mgmt_ring __iomem *hdr = ring->base;

	return readl(&hdr->version);
}

/* Number of pending descriptors (producer perspective) */
static inline u32 amdxdna_ipc_mgmt_count(struct amdxdna_ipc_ring *ring)
{
	struct amdxdna_ipc_mgmt_ring __iomem *hdr = ring->base;

	return ring->cached_idx - readl(&hdr->tail);
}

/* Read the buffer pool offset for a given descriptor index */
static inline u32 amdxdna_ipc_mgmt_buf_offset(struct amdxdna_ipc_ring *ring,
					       u32 desc_idx)
{
	struct amdxdna_ipc_mgmt_ring __iomem *hdr = ring->base;

	return readl(&hdr->desc[desc_idx]);
}

/*
 * Claim a descriptor slot at head (producer side).
 * Does NOT advance head -- caller fills the buffer then calls submit.
 * Returns descriptor index (>=0) or -ENOSPC.
 */
static inline int amdxdna_ipc_mgmt_alloc_desc(struct amdxdna_ipc_ring *ring)
{
	struct amdxdna_ipc_mgmt_ring __iomem *hdr = ring->base;
	u32 head = ring->cached_idx;
	u32 tail = smp_load_acquire(&hdr->tail);

	if (head - tail > ring->mask)
		return -ENOSPC;

	return head & ring->mask;
}

/*
 * Advance head by 1 to make the last allocated descriptor visible.
 * Must be called after the producer has written the message into the buffer.
 */
static inline void amdxdna_ipc_mgmt_submit(struct amdxdna_ipc_ring *ring)
{
	struct amdxdna_ipc_mgmt_ring __iomem *hdr = ring->base;

	dma_wmb();
	ring->cached_idx++;
	smp_store_release(&hdr->head, ring->cached_idx);
}

/*
 * Advance head by count to commit multiple descriptors at once.
 * One barrier + one index publish for the entire batch.
 */
static inline void amdxdna_ipc_mgmt_submit_n(struct amdxdna_ipc_ring *ring,
					      u32 count)
{
	struct amdxdna_ipc_mgmt_ring __iomem *hdr = ring->base;

	dma_wmb();
	ring->cached_idx += count;
	smp_store_release(&hdr->head, ring->cached_idx);
}

/*
 * Peek at the next pending descriptor (consumer side).
 * Does NOT advance tail -- caller processes then calls complete.
 * Returns descriptor index (>=0) or -ENODATA.
 */
static inline int amdxdna_ipc_mgmt_consume(struct amdxdna_ipc_ring *ring)
{
	struct amdxdna_ipc_mgmt_ring __iomem *hdr = ring->base;
	u32 head = smp_load_acquire(&hdr->head);
	u32 tail = ring->cached_idx;

	if (head == tail)
		return -ENODATA;

	dma_rmb();
	return tail & ring->mask;
}

/* Advance tail by 1 to release a consumed descriptor. */
static inline void amdxdna_ipc_mgmt_complete(struct amdxdna_ipc_ring *ring)
{
	struct amdxdna_ipc_mgmt_ring __iomem *hdr = ring->base;

	ring->cached_idx++;
	smp_store_release(&hdr->tail, ring->cached_idx);
}

/*
 * Drain up to max_count descriptors (consumer). One barrier + one tail publish.
 * Writes raw free-running indices to out_indices[]; caller uses & ring->mask
 * to get the slot index. Returns number of entries consumed (0 if empty).
 */
static inline u32 amdxdna_ipc_mgmt_drain(struct amdxdna_ipc_ring *ring,
					  u32 *out_indices, u32 max_count)
{
	struct amdxdna_ipc_mgmt_ring __iomem *hdr = ring->base;
	u32 head = smp_load_acquire(&hdr->head);
	u32 tail = ring->cached_idx;
	u32 avail = head - tail;
	u32 count, i;

	if (!avail)
		return 0;

	count = min(avail, max_count);
	dma_rmb();
	for (i = 0; i < count; i++)
		out_indices[i] = (tail + i) & ring->mask;

	ring->cached_idx = tail + count;
	smp_store_release(&hdr->tail, tail + count);
	return count;
}

/*
 * Check whether the producer should kick the peer (event-index pattern).
 * Returns true if cached_idx (new head) has crossed the consumer's
 * notify_idx threshold.
 */
static inline bool amdxdna_ipc_mgmt_need_kick(struct amdxdna_ipc_ring *ring)
{
	struct amdxdna_ipc_mgmt_ring __iomem *hdr = ring->base;

	return ring->cached_idx == readl(&hdr->notify_idx);
}

/*
 * Set the notification threshold (consumer side).
 * Tells producer: "kick me when your head reaches idx."
 */
static inline void amdxdna_ipc_mgmt_set_notify(struct amdxdna_ipc_ring *ring,
						u32 idx)
{
	struct amdxdna_ipc_mgmt_ring __iomem *hdr = ring->base;

	smp_store_release(&hdr->notify_idx, idx);
}

#endif /* _AMDXDNA_IPC_MGMT_H_ */
