/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 *
 * Direct MMIO/PLM helpers for AIE registers — bypasses ai_engine driver APIs.
 * Used to validate which operations can be replaced with direct APU access.
 * Guarded by CONFIG_AMDXDNA_DEBUG_TEST.
 */

#ifndef _AMDXDNA_CERT_LOADER_H_
#define _AMDXDNA_CERT_LOADER_H_

#ifdef CONFIG_AMDXDNA_DEBUG_TEST

#include <linux/types.h>

struct amdxdna_dev;

/*
 * VE2 / AIE2PS external offsets (as seen from APU through AIE aperture).
 */
#define CERT_COL_STRIDE			0x2000000ULL	/* col << 25 */
#define CERT_CORE_CTRL_REG		0xC0004U	/* UC Core Control register */
#define CERT_EVENT_GEN_OFF		0x34008U	/* PL Event Generate register */
#define CERT_USER_EVENT_ID		0xB6U		/* USER_EVENT_0 for doorbell */

/* AIE aperture base (40-bit physical, VE2/VEK385) */
#define AIE_APERTURE_BASE		0x20000000000ULL
#define AIE_APERTURE_SIZE		0x40000000ULL	/* 1 GB */

int amdxdna_cert_init(struct amdxdna_dev *xdna);
void amdxdna_cert_fini(struct amdxdna_dev *xdna);

int amdxdna_cert_manual_doorbell(struct amdxdna_dev *xdna, u32 start_col,
				 u32 num_col);

int amdxdna_cert_manual_wakeup(struct amdxdna_dev *xdna, u32 start_col,
			       u32 num_col);

int amdxdna_cert_manual_handshake(struct amdxdna_dev *xdna,
				  void *hs_data, size_t hs_size,
				  u32 col);

#else /* !CONFIG_AMDXDNA_DEBUG_TEST */

static inline int amdxdna_cert_init(struct amdxdna_dev *xdna) { return 0; }
static inline void amdxdna_cert_fini(struct amdxdna_dev *xdna) {}
static inline int amdxdna_cert_manual_doorbell(struct amdxdna_dev *xdna,
					       u32 start_col, u32 num_col)
{ return 0; }
static inline int amdxdna_cert_manual_wakeup(struct amdxdna_dev *xdna,
					     u32 start_col, u32 num_col)
{ return 0; }
static inline int amdxdna_cert_manual_handshake(struct amdxdna_dev *xdna,
						void *hs_data, size_t hs_size,
						u32 col)
{ return 0; }

#endif /* CONFIG_AMDXDNA_DEBUG_TEST */

#endif /* _AMDXDNA_CERT_LOADER_H_ */
