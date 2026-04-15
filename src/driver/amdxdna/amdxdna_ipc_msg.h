/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#ifndef _AMDXDNA_IPC_MSG_H_
#define _AMDXDNA_IPC_MSG_H_

#include "amdxdna_ipc.h"

/* Protocol version -- bump major for breaking changes, minor for additions */
#define AMDXDNA_IPC_PROTO_MAJOR		1
#define AMDXDNA_IPC_PROTO_MINOR		0
#define AMDXDNA_IPC_PROTO_VERSION	\
	((AMDXDNA_IPC_PROTO_MAJOR << 16) | AMDXDNA_IPC_PROTO_MINOR)

/*
 * Management message opcodes (HW Domain <-> management processor)
 *
 * Opcode rules:
 *   - Opcodes cannot be changed once added
 *   - Obsoleted opcodes return AMDXDNA_IPC_STATUS_NOTSUPP
 *   - Add new opcodes for new operations
 */
enum amdxdna_ipc_msg_opcode {
	AMDXDNA_IPC_OP_IDENTIFY			= 0x1001,
	AMDXDNA_IPC_OP_CREATE_PARTITION		= 0x1002,
	AMDXDNA_IPC_OP_DESTROY_PARTITION	= 0x1003,
	AMDXDNA_IPC_OP_CREATE_HW_CONTEXT	= 0x1004,
	AMDXDNA_IPC_OP_DESTROY_HW_CONTEXT	= 0x1005,
};

/* Response status codes */
enum amdxdna_ipc_msg_status {
	AMDXDNA_IPC_STATUS_SUCCESS	= 0x0,
	AMDXDNA_IPC_STATUS_ERROR	= 0x1,
	AMDXDNA_IPC_STATUS_NOTSUPP	= 0x2,
};

/*
 * Request/response structures -- stubs, to be extended as the management
 * protocol is finalized. Packed to 4-byte alignment to guarantee identical
 * wire layout across all HW Domains.
 */

#pragma pack(push, 4)

/* AMDXDNA_IPC_OP_IDENTIFY */
struct amdxdna_ipc_identify_req {
	u32 reserved;
};

struct amdxdna_ipc_identify_resp {
	u32 status;
	u32 fw_major;
	u32 fw_minor;
	u32 fw_patch;
};

/* AMDXDNA_IPC_OP_CREATE_PARTITION */
struct amdxdna_ipc_create_partition_req {
	u32 col_start;
	u32 col_count;
};

struct amdxdna_ipc_create_partition_resp {
	u32 status;
	u32 partition_id;
};

/* AMDXDNA_IPC_OP_DESTROY_PARTITION */
struct amdxdna_ipc_destroy_partition_req {
	u32 partition_id;
};

struct amdxdna_ipc_destroy_partition_resp {
	u32 status;
};

/* AMDXDNA_IPC_OP_CREATE_HW_CONTEXT */
struct amdxdna_ipc_create_hw_ctx_req {
	u32 partition_id;
	u32 num_tiles;
	u32 hsa_addr_hi;
	u32 hsa_addr_lo;
};

struct amdxdna_ipc_create_hw_ctx_resp {
	u32 status;
	u32 hw_context_id;
};

/* AMDXDNA_IPC_OP_DESTROY_HW_CONTEXT */
struct amdxdna_ipc_destroy_hw_ctx_req {
	u32 hw_context_id;
};

struct amdxdna_ipc_destroy_hw_ctx_resp {
	u32 status;
};

#pragma pack(pop)

#endif /* _AMDXDNA_IPC_MSG_H_ */
