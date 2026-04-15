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
 * Shared-memory ring layout:
 *
 *   offset  field
 *   0x00    head (u32)         producer write position
 *   0x04    tail (u32)         consumer read position
 *   0x08    ring_mask (u32)    N-1, where N is power-of-2 descriptor count
 *   0x0C    buf_size (u32)     size of each buffer in the pool
 *   0x10    buf_base_lo (u32)  buffer pool base address (low 32 bits)
 *   0x14    buf_base_hi (u32)  buffer pool base address (high 32 bits)
 *   0x18    version (u32)      protocol version (checked once at init)
 *   0x1C    reserved (u32)     alignment
 *   0x20    desc[0] (u32)      first descriptor (offset into buffer pool)
 *   0x24    desc[1] (u32)      ...
 *   ...
 *
 * Separate buffer pool (at buf_base, not contiguous with the ring):
 *
 *   buf[i] = buf_base + i * buf_size
 *
 * Each buffer contains an amdxdna_ipc_msg_hdr followed by payload.
 *
 * Producer (HW Domain) owns head; consumer (management processor) owns tail.
 */

#include "amdxdna_ipc.h"

/* ring header field offsets */
#define AMDXDNA_IPC_MGMT_HEAD_OFF	0x00
#define AMDXDNA_IPC_MGMT_TAIL_OFF	0x04
#define AMDXDNA_IPC_MGMT_MASK_OFF	0x08
#define AMDXDNA_IPC_MGMT_BUFSZ_OFF	0x0C
#define AMDXDNA_IPC_MGMT_BASELO_OFF	0x10
#define AMDXDNA_IPC_MGMT_BASEHI_OFF	0x14
#define AMDXDNA_IPC_MGMT_VER_OFF	0x18
#define AMDXDNA_IPC_MGMT_RSVD_OFF	0x1C
#define AMDXDNA_IPC_MGMT_DESC_OFF	0x20

/* message header field offsets (within each buffer in the pool) */
#define AMDXDNA_IPC_MSG_OPCODE_OFF	0x00
#define AMDXDNA_IPC_MSG_ID_OFF		0x04
#define AMDXDNA_IPC_MSG_SIZE_OFF	0x08
#define AMDXDNA_IPC_MSG_FLAGS_OFF	0x0C
#define AMDXDNA_IPC_MSG_HDR_SIZE	0x10	/* 16 bytes */

/*
 * Per-message header in each mgmt buffer.
 * Actual access goes through ops callbacks since the buffers reside in shared memory.
 */
struct amdxdna_ipc_msg_hdr {
	u32 opcode;  /* message opcode (see amdxdna_ipc_msg.h) */
	u32 msg_id;  /* request/response correlation ID */
	u32 size;    /* payload size in bytes (excludes this header) */
	u32 flags;   /* bitfield (response, error, etc.) */
};

#define AMDXDNA_IPC_MSG_FLAG_RESPONSE	BIT(0)
#define AMDXDNA_IPC_MSG_FLAG_ERROR	BIT(1)

static inline u32 amdxdna_ipc_mgmt_desc_off(u32 idx)
{
	return AMDXDNA_IPC_MGMT_DESC_OFF + idx * sizeof(u32);
}

/*
 * Initialize a management BDR ring in shared memory.
 * Zeroes head/tail, writes ring metadata, and initializes each descriptor
 * to point at its corresponding buffer (desc[i] = i * buf_size).
 * num_descs must be a power of 2.  Returns 0 or -EINVAL.
 */
static inline int amdxdna_ipc_mgmt_init(const struct amdxdna_ipc_ring_ctx *ctx,
					 u32 num_descs, u32 buf_size,
					 u32 buf_base_lo, u32 buf_base_hi,
					 u32 version)
{
	u32 i;

	if (!num_descs || (num_descs & (num_descs - 1)))
		return -EINVAL;

	amdxdna_ipc_write32(ctx, AMDXDNA_IPC_MGMT_HEAD_OFF, 0);
	amdxdna_ipc_write32(ctx, AMDXDNA_IPC_MGMT_TAIL_OFF, 0);
	amdxdna_ipc_write32(ctx, AMDXDNA_IPC_MGMT_MASK_OFF, num_descs - 1);
	amdxdna_ipc_write32(ctx, AMDXDNA_IPC_MGMT_BUFSZ_OFF, buf_size);
	amdxdna_ipc_write32(ctx, AMDXDNA_IPC_MGMT_BASELO_OFF, buf_base_lo);
	amdxdna_ipc_write32(ctx, AMDXDNA_IPC_MGMT_BASEHI_OFF, buf_base_hi);
	amdxdna_ipc_write32(ctx, AMDXDNA_IPC_MGMT_VER_OFF, version);
	amdxdna_ipc_write32(ctx, AMDXDNA_IPC_MGMT_RSVD_OFF, 0);

	for (i = 0; i < num_descs; i++)
		amdxdna_ipc_write32(ctx, amdxdna_ipc_mgmt_desc_off(i),
				    i * buf_size);

	return 0;
}

/* Read protocol version from ring header */
static inline u32 amdxdna_ipc_mgmt_get_version(const struct amdxdna_ipc_ring_ctx *ctx)
{
	return amdxdna_ipc_read32(ctx, AMDXDNA_IPC_MGMT_VER_OFF);
}

/* Check if ring has no pending descriptors */
static inline int amdxdna_ipc_mgmt_is_empty(const struct amdxdna_ipc_ring_ctx *ctx)
{
	u32 head = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_MGMT_HEAD_OFF);
	u32 tail = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_MGMT_TAIL_OFF);

	return head == tail;
}

/* Check if ring is full */
static inline int amdxdna_ipc_mgmt_is_full(const struct amdxdna_ipc_ring_ctx *ctx)
{
	u32 head = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_MGMT_HEAD_OFF);
	u32 tail = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_MGMT_TAIL_OFF);
	u32 mask = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_MGMT_MASK_OFF);

	return ((head - tail) & mask) == mask;
}

/* Number of pending descriptors in the ring */
static inline u32 amdxdna_ipc_mgmt_count(const struct amdxdna_ipc_ring_ctx *ctx)
{
	u32 head = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_MGMT_HEAD_OFF);
	u32 tail = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_MGMT_TAIL_OFF);
	u32 mask = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_MGMT_MASK_OFF);

	return (head - tail) & mask;
}

/* Compute buffer pool offset for a descriptor at desc_idx */
static inline u32 amdxdna_ipc_mgmt_buf_offset(const struct amdxdna_ipc_ring_ctx *ctx,
					       u32 desc_idx)
{
	return amdxdna_ipc_read32(ctx, amdxdna_ipc_mgmt_desc_off(desc_idx));
}

/*
 * Claim a descriptor slot at head (producer side).
 * Does NOT advance head -- caller fills the buffer then calls
 * amdxdna_ipc_mgmt_submit().  Returns 0 or -ENOSPC.
 */
static inline int amdxdna_ipc_mgmt_alloc_desc(const struct amdxdna_ipc_ring_ctx *ctx,
					       u32 *desc_idx)
{
	u32 head = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_MGMT_HEAD_OFF);
	u32 tail = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_MGMT_TAIL_OFF);
	u32 mask = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_MGMT_MASK_OFF);

	if (((head - tail) & mask) == mask)
		return -ENOSPC;

	*desc_idx = head & mask;
	return 0;
}

/*
 * Advance head to make the last allocated descriptor visible to the consumer.
 * Must be called after the producer has written the message into the buffer.
 */
static inline void amdxdna_ipc_mgmt_submit(const struct amdxdna_ipc_ring_ctx *ctx)
{
	u32 head = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_MGMT_HEAD_OFF);

	amdxdna_ipc_write32(ctx, AMDXDNA_IPC_MGMT_HEAD_OFF, head + 1);
}

/*
 * Read the next pending descriptor (consumer side).
 * Does NOT advance tail -- caller processes the message then calls
 * amdxdna_ipc_mgmt_complete().  Returns 0 or -ENODATA.
 */
static inline int amdxdna_ipc_mgmt_consume(const struct amdxdna_ipc_ring_ctx *ctx,
					    u32 *desc_idx)
{
	u32 head = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_MGMT_HEAD_OFF);
	u32 tail = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_MGMT_TAIL_OFF);
	u32 mask = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_MGMT_MASK_OFF);

	if (head == tail)
		return -ENODATA;

	*desc_idx = tail & mask;
	return 0;
}

/*
 * Advance tail to release a consumed descriptor.
 * Must be called after the consumer has finished processing the message.
 */
static inline void amdxdna_ipc_mgmt_complete(const struct amdxdna_ipc_ring_ctx *ctx)
{
	u32 tail = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_MGMT_TAIL_OFF);

	amdxdna_ipc_write32(ctx, AMDXDNA_IPC_MGMT_TAIL_OFF, tail + 1);
}

#endif /* _AMDXDNA_IPC_MGMT_H_ */
