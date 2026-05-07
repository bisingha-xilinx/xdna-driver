// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 *
 * Manual AIE register access for CERT — bypasses ai_engine driver APIs.
 * Used to validate which parts of the ai_engine flow can be replaced with
 * direct APU MMIO writes (for porting to T20 where ai_engine is unavailable).
 *
 * Guarded by CONFIG_AMDXDNA_DEBUG_TEST.
 */

#ifdef CONFIG_AMDXDNA_DEBUG_TEST

#include <linux/device.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/firmware/xlnx-zynqmp.h>

#include "amdxdna_drm.h"
#include "amdxdna_cert_loader.h"

/* PLM AIE device node ID (same as PM_DEV_AIE in RPU firmware) */
#define PLM_DEV_AIE		0x18224072U

/* PLM TLV operation types used by handshake */
#define PLM_OPS_HANDSHAKE	5U
#define PLM_OPS_START_NUM_COL	7U

/* TLV packet structures matching PLM expectations (all __aligned(4)) */
struct plm_op_start_col {
	u16 type;
	u16 len;
	u16 start_col;
	u16 num_col;
} __aligned(4);

struct plm_op_handshake {
	u16 type;
	u16 len;
	u32 offset;
	u32 hi_addr;
	u32 lo_addr;
} __aligned(4);

#define PLM_PKT_MAX_SIZE	200U

struct cert_loader_state {
	void __iomem *aie_base;
};

static struct cert_loader_state loader_state;

/* ─── Public API ──────────────────────────────────────────────────────── */

int amdxdna_cert_init(struct amdxdna_dev *xdna)
{
	struct device *dev = xdna->ddev.dev;

	loader_state.aie_base = devm_ioremap(dev, AIE_APERTURE_BASE,
					     AIE_APERTURE_SIZE);
	if (!loader_state.aie_base) {
		XDNA_ERR(xdna, "Failed to map AIE aperture at 0x%llx",
			 AIE_APERTURE_BASE);
		return -ENOMEM;
	}

	XDNA_INFO(xdna, "CERT loader init: AIE aperture mapped at 0x%llx (1GB)",
		  AIE_APERTURE_BASE);
	return 0;
}

void amdxdna_cert_fini(struct amdxdna_dev *xdna)
{
	loader_state.aie_base = NULL;
}

int amdxdna_cert_manual_doorbell(struct amdxdna_dev *xdna, u32 start_col,
				 u32 num_col)
{
	u32 col;

	if (!loader_state.aie_base) {
		XDNA_ERR(xdna, "CERT loader not initialized");
		return -ENODEV;
	}

	for (col = start_col; col < start_col + num_col; col++) {
		void __iomem *evtgen = loader_state.aie_base +
				       (u64)col * CERT_COL_STRIDE +
				       CERT_EVENT_GEN_OFF;

		iowrite32(CERT_USER_EVENT_ID, evtgen);
	}

	XDNA_INFO(xdna, "manual doorbell sent to cols [%u..%u]",
		 start_col, start_col + num_col - 1);
	return 0;
}

int amdxdna_cert_manual_wakeup(struct amdxdna_dev *xdna, u32 start_col,
			       u32 num_col)
{
	u32 col;

	if (!loader_state.aie_base) {
		XDNA_ERR(xdna, "CERT loader not initialized");
		return -ENODEV;
	}

	for (col = start_col + num_col; col-- > start_col; ) {
		void __iomem *ctrl = loader_state.aie_base +
				     (u64)col * CERT_COL_STRIDE +
				     CERT_CORE_CTRL_REG;

		iowrite32(BIT(0), ctrl);
	}

	XDNA_INFO(xdna, "manual wakeup sent to cols [%u..%u]",
		 start_col, start_col + num_col - 1);
	return 0;
}

/*
 * Send handshake data to one column via PLM DMA.
 * Builds a TLV packet (START_COL + HANDSHAKE op) and calls PLM directly.
 */
static int cert_plm_handshake_col(struct amdxdna_dev *xdna,
				  void *hs_buf, size_t hs_size, u32 col)
{
	struct device *dev = xdna->ddev.dev;
	struct plm_op_start_col *hdr;
	struct plm_op_handshake *op;
	dma_addr_t pkt_dma, hs_dma;
	u32 pkt_size;
	void *pkt_va, *hs_va;
	int ret;

	pkt_size = sizeof(*hdr) + sizeof(*op);

	pkt_va = dma_alloc_coherent(dev, PLM_PKT_MAX_SIZE, &pkt_dma,
				    GFP_KERNEL);
	if (!pkt_va)
		return -ENOMEM;

	hs_va = dma_alloc_coherent(dev, hs_size, &hs_dma, GFP_KERNEL);
	if (!hs_va) {
		dma_free_coherent(dev, PLM_PKT_MAX_SIZE, pkt_va, pkt_dma);
		return -ENOMEM;
	}

	memcpy(hs_va, hs_buf, hs_size);

	memset(pkt_va, 0, PLM_PKT_MAX_SIZE);

	hdr = pkt_va;
	hdr->type = PLM_OPS_START_NUM_COL;
	hdr->len = sizeof(*hdr);
	hdr->start_col = col;
	hdr->num_col = 1;

	op = pkt_va + sizeof(*hdr);
	op->type = PLM_OPS_HANDSHAKE;
	op->len = sizeof(*op) + (u16)hs_size;
	op->offset = 0;
	op->hi_addr = upper_32_bits(hs_dma);
	op->lo_addr = lower_32_bits(hs_dma);

	XDNA_INFO(xdna, "PLM handshake col %u: pkt_dma=0x%llx hs_dma=0x%llx size=%zu",
		  col, (u64)pkt_dma, (u64)hs_dma, hs_size);

	ret = versal2_pm_aie2ps_operation(PLM_DEV_AIE, pkt_size,
					  upper_32_bits(pkt_dma),
					  lower_32_bits(pkt_dma));
	if (ret)
		XDNA_ERR(xdna, "PLM handshake failed col %u: %d", col, ret);

	dma_free_coherent(dev, hs_size, hs_va, hs_dma);
	dma_free_coherent(dev, PLM_PKT_MAX_SIZE, pkt_va, pkt_dma);

	return ret;
}

int amdxdna_cert_manual_handshake(struct amdxdna_dev *xdna,
				  void *hs_data, size_t hs_size,
				  u32 col)
{
	return cert_plm_handshake_col(xdna, hs_data, hs_size, col);
}

#endif /* CONFIG_AMDXDNA_DEBUG_TEST */
