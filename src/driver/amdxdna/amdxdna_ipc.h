/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#ifndef _AMDXDNA_IPC_H_
#define _AMDXDNA_IPC_H_

#if defined(__KERNEL__)
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/bits.h>
#elif defined(__cplusplus)
#include <cstdint>
#include <cerrno>
typedef uint32_t u32;
typedef uint64_t u64;
#ifndef BIT
#define BIT(nr)		(1u << (nr))
#endif
#else
#include <stdint.h>
#include <errno.h>
typedef uint32_t u32;
typedef uint64_t u64;
#ifndef BIT
#define BIT(nr)		(1u << (nr))
#endif
#endif

/*
 * Platform-specific shared-memory access callbacks.
 * Each HW Domain provides its own read32/write32 implementation.
 */
struct amdxdna_ipc_ops {
	u32  (*read32)(void *priv, u32 offset);  /* read u32 at byte offset */
	void (*write32)(void *priv, u32 offset, u32 val); /* write u32 */
};

/* Per-ring instance context */
struct amdxdna_ipc_ring_ctx {
	void				*priv; /* platform context (e.g., ioremap base) */
	const struct amdxdna_ipc_ops	*ops;  /* platform read/write callbacks */
};

static inline u32 amdxdna_ipc_read32(const struct amdxdna_ipc_ring_ctx *ctx,
				      u32 offset)
{
	return ctx->ops->read32(ctx->priv, offset);
}

static inline void amdxdna_ipc_write32(const struct amdxdna_ipc_ring_ctx *ctx,
					u32 offset, u32 val)
{
	ctx->ops->write32(ctx->priv, offset, val);
}

#endif /* _AMDXDNA_IPC_H_ */
