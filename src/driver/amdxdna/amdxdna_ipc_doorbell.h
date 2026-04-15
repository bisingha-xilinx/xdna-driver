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
 * Shared-memory layout:
 *
 *   offset  field
 *   0x00    head (u32)      producer write position
 *   0x04    tail (u32)      consumer read position
 *   0x08    ring_mask (u32) N-1, where N is power-of-2 ring size
 *   0x0C    data[0] (u32)   first ring entry
 *   0x10    data[1] (u32)   ...
 *   ...
 *
 * Producer (HW Domain) owns head; consumer (management processor) owns tail.
 * Each data entry is a u32 (e.g., hw_context_id).
 */

#include "amdxdna_ipc.h"

#define AMDXDNA_IPC_DB_HEAD_OFF		0x00
#define AMDXDNA_IPC_DB_TAIL_OFF		0x04
#define AMDXDNA_IPC_DB_MASK_OFF		0x08
#define AMDXDNA_IPC_DB_DATA_OFF		0x0C

static inline u32 amdxdna_ipc_db_data_off(u32 idx)
{
	return AMDXDNA_IPC_DB_DATA_OFF + idx * sizeof(u32);
}

/*
 * Initialize a doorbell ring in shared memory.
 * Zeroes head and tail, writes ring_mask = num_entries - 1.
 * num_entries must be a power of 2.  Returns 0 or -EINVAL.
 */
static inline int amdxdna_ipc_doorbell_init(const struct amdxdna_ipc_ring_ctx *ctx,
					    u32 num_entries)
{
	if (!num_entries || (num_entries & (num_entries - 1)))
		return -EINVAL;

	amdxdna_ipc_write32(ctx, AMDXDNA_IPC_DB_HEAD_OFF, 0);
	amdxdna_ipc_write32(ctx, AMDXDNA_IPC_DB_TAIL_OFF, 0);
	amdxdna_ipc_write32(ctx, AMDXDNA_IPC_DB_MASK_OFF, num_entries - 1);

	return 0;
}

/* Check if ring has no pending entries */
static inline int amdxdna_ipc_doorbell_is_empty(const struct amdxdna_ipc_ring_ctx *ctx)
{
	u32 head = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_DB_HEAD_OFF);
	u32 tail = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_DB_TAIL_OFF);

	return head == tail;
}

/* Check if ring is full */
static inline int amdxdna_ipc_doorbell_is_full(const struct amdxdna_ipc_ring_ctx *ctx)
{
	u32 head = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_DB_HEAD_OFF);
	u32 tail = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_DB_TAIL_OFF);
	u32 mask = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_DB_MASK_OFF);

	return ((head - tail) & mask) == mask;
}

/* Number of pending entries in the ring */
static inline u32 amdxdna_ipc_doorbell_count(const struct amdxdna_ipc_ring_ctx *ctx)
{
	u32 head = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_DB_HEAD_OFF);
	u32 tail = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_DB_TAIL_OFF);
	u32 mask = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_DB_MASK_OFF);

	return (head - tail) & mask;
}

/*
 * Enqueue a value into the doorbell ring (producer side).
 * Writes value at head position and advances head.
 * Returns 0 on success, -ENOSPC if ring is full.
 */
static inline int amdxdna_ipc_doorbell_produce(const struct amdxdna_ipc_ring_ctx *ctx,
					       u32 value)
{
	u32 head = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_DB_HEAD_OFF);
	u32 tail = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_DB_TAIL_OFF);
	u32 mask = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_DB_MASK_OFF);

	if (((head - tail) & mask) == mask)
		return -ENOSPC;

	amdxdna_ipc_write32(ctx, amdxdna_ipc_db_data_off(head & mask), value);

	/* Ensure data is written before head is advanced */
	amdxdna_ipc_write32(ctx, AMDXDNA_IPC_DB_HEAD_OFF, head + 1);

	return 0;
}

/*
 * Dequeue a value from the doorbell ring (consumer side).
 * Reads value at tail position and advances tail.
 * Returns 0 on success, -ENODATA if ring is empty.
 */
static inline int amdxdna_ipc_doorbell_consume(const struct amdxdna_ipc_ring_ctx *ctx,
					       u32 *value)
{
	u32 head = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_DB_HEAD_OFF);
	u32 tail = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_DB_TAIL_OFF);
	u32 mask = amdxdna_ipc_read32(ctx, AMDXDNA_IPC_DB_MASK_OFF);

	if (head == tail)
		return -ENODATA;

	*value = amdxdna_ipc_read32(ctx, amdxdna_ipc_db_data_off(tail & mask));

	/* Ensure data is read before tail is advanced */
	amdxdna_ipc_write32(ctx, AMDXDNA_IPC_DB_TAIL_OFF, tail + 1);

	return 0;
}

#endif /* _AMDXDNA_IPC_DOORBELL_H_ */
