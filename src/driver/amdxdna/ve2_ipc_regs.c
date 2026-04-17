// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#include "drm_local/amdxdna_accel.h"
#include "aie4_pci.h"

extern const struct amdxdna_dev_ops ve2_ipc_ops;

static const struct amdxdna_dev_priv ve2_ipc_dev_priv = {
	.npufw_path	= "amdnpu/ve2_ipc_fw.elf",
	.certfw_path	= "amdnpu/ve2_ipc_cert.elf",
};

const struct amdxdna_dev_info dev_ve2_ipc_info = {
	.device_type	= AMDXDNA_DEV_TYPE_KMQ,
	.dev_priv	= &ve2_ipc_dev_priv,
	.ops		= &ve2_ipc_ops,
};
