/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#ifndef _VE2_IPC_PLAT_H_
#define _VE2_IPC_PLAT_H_

#include "aie4_pci.h"
#include "amdxdna_ipc_doorbell.h"

struct ve2_ipc_priv {
	struct amdxdna_dev_hdl		*ndev;
	struct amdxdna_ipc_ring		db_ring;
	void __iomem			*ocm_base;
	resource_size_t			ocm_size;
	int				ipi_irq;
	spinlock_t			db_lock;
	struct timer_list		poll_timer;
};

extern const struct amdxdna_dev_ops ve2_ipc_ops;

#endif /* _VE2_IPC_PLAT_H_ */
