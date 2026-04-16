/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#ifndef _AMDXDNA_IPC_H_
#define _AMDXDNA_IPC_H_

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/bits.h>
#include <asm/barrier.h>

/*
 * Local ring context -- per-side cached state, never in shared memory.
 *
 * Each side (producer or consumer) keeps its own instance.
 * The cached_idx holds the locally-written index:
 *   - For producer: cached head (only producer advances head)
 *   - For consumer: cached tail (only consumer advances tail)
 *
 * This eliminates MMIO reads of the locally-owned pointer and follows
 * the same caching pattern as amdxdna_mailbox.c (x2i_tail / i2x_head).
 */
struct amdxdna_ipc_ring {
	void __iomem	*base;		/* ioremap'd shared-memory base */
	u32		mask;		/* ring_mask (N-1), cached at init */
	u32		cached_idx;	/* locally-owned index (head or tail) */
};

#endif /* _AMDXDNA_IPC_H_ */
