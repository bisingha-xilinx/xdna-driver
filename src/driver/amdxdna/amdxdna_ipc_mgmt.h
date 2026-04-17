/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#ifndef _AMDXDNA_IPC_MGMT_H_
#define _AMDXDNA_IPC_MGMT_H_

/*
 * Management buffer-descriptor ring (BDR) shared-memory layout.
 *
 * This header defines only the wire-level layout that both the Linux driver
 * and the management processor must agree on.  All ring I/O (enqueue,
 * dequeue, message ID tracking, IRQ dispatch) is handled by
 * amdxdna_mailbox.c which already provides these operations.
 *
 * Separate buffer pool (at buf_base, not contiguous with the ring):
 *   buf[i] = buf_base + i * buf_size
 *
 * Producer (HW Domain) owns head; consumer (management processor) owns tail.
 */

#include <linux/types.h>

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

#endif /* _AMDXDNA_IPC_MGMT_H_ */
