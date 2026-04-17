// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/moduleparam.h>

#include "ve2_ipc_plat.h"
#include "amdxdna_mailbox.h"
#include "amdxdna_ipc_mgmt.h"

#define VE2_IPC_POLL_INTERVAL_MS	10

static int ve2_ipc_polling;
module_param(ve2_ipc_polling, int, 0644);
MODULE_PARM_DESC(ve2_ipc_polling, "Enable VE2-IPC polling mode for bringup");

/*
 * Notification suppression helpers (moved from amdxdna_ipc_mgmt.h).
 * These operate on the doorbell ring's notify_idx field.
 */
static bool ve2_ipc_db_need_kick(struct amdxdna_ipc_ring *ring)
{
	struct amdxdna_ipc_db_ring __iomem *hdr = ring->base;

	return ring->cached_idx == readl(&hdr->notify_idx);
}

static void ve2_ipc_trigger_ipi(struct ve2_ipc_priv *priv)
{
	/* TODO: write to IPI trigger register from DT */
}

static void ve2_ipc_ring_doorbell(struct amdxdna_ctx *ctx)
{
	struct amdxdna_dev *xdna = ctx->client->xdna;
	struct amdxdna_dev_hdl *ndev = xdna->dev_handle;
	struct ve2_ipc_priv *priv = (struct ve2_ipc_priv *)ndev->priv_data;
	unsigned long flags;

	spin_lock_irqsave(&priv->db_lock, flags);
	if (amdxdna_ipc_db_produce(&priv->db_ring, ctx->priv->hw_ctx_id))
		XDNA_ERR(xdna, "Doorbell ring full for ctx %d", ctx->priv->hw_ctx_id);

	if (ve2_ipc_db_need_kick(&priv->db_ring))
		ve2_ipc_trigger_ipi(priv);
	spin_unlock_irqrestore(&priv->db_lock, flags);
}

static void ve2_ipc_poll_timer_fn(struct timer_list *t)
{
	struct ve2_ipc_priv *priv = from_timer(priv, t, poll_timer);
	struct amdxdna_dev_hdl *ndev = priv->ndev;
	struct cert_comp *cert_comp;
	unsigned long idx;

	/* 1. Poll CERT completions -- wake up all waiters */
	xa_lock(&ndev->cert_comp_xa);
	xa_for_each(&ndev->cert_comp_xa, idx, cert_comp)
		wake_up_all(&cert_comp->waitq);
	xa_unlock(&ndev->cert_comp_xa);

	/* 2. Poll mailbox rx path for management responses */
	xdna_mailbox_poll_channel(ndev->mgmt_chann);

	/*
	 * 3. Doorbell ring poll: the management processor consumes doorbell
	 * entries independently.  Completions come back via the mailbox
	 * (polled above) or CERT path (polled above), so no additional
	 * doorbell-specific polling action is needed here.
	 */

	mod_timer(&priv->poll_timer,
		  jiffies + msecs_to_jiffies(VE2_IPC_POLL_INTERVAL_MS));
}

static int ve2_ipc_init(struct amdxdna_dev *xdna)
{
	struct platform_device *pdev = to_platform_device(xdna->ddev.dev);
	struct amdxdna_dev_hdl *ndev;
	struct ve2_ipc_priv *priv;
	struct resource *res;
	int ret;

	ndev = devm_kzalloc(&pdev->dev, sizeof(*ndev), GFP_KERNEL);
	if (!ndev)
		return -ENOMEM;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ndev->priv = xdna->dev_info->dev_priv;
	ndev->xdna = xdna;
	ndev->priv_data = priv;
	priv->ndev = ndev;
	xdna->dev_handle = ndev;
	mutex_init(&ndev->aie4_lock);
	xa_init(&ndev->cert_comp_xa);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->ocm_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->ocm_base)) {
		ret = PTR_ERR(priv->ocm_base);
		XDNA_ERR(xdna, "Failed to map shared memory, ret %d", ret);
		goto err_fini;
	}
	priv->ocm_size = resource_size(res);

	priv->ipi_irq = platform_get_irq(pdev, 0);
	if (priv->ipi_irq < 0) {
		XDNA_INFO(xdna, "No IPI IRQ, using polling mode");
		priv->ipi_irq = -1;
	}

	spin_lock_init(&priv->db_lock);

	ret = amdxdna_ipc_db_init(&priv->db_ring, priv->ocm_base,
				   AMDXDNA_IPC_MGMT_NUM_DESCS);
	if (ret) {
		XDNA_ERR(xdna, "Doorbell ring init failed, ret %d", ret);
		goto err_fini;
	}

	timer_setup(&priv->poll_timer, ve2_ipc_poll_timer_fn, 0);
	if (ve2_ipc_polling || priv->ipi_irq < 0) {
		XDNA_INFO(xdna, "VE2-IPC: polling mode enabled (interval %dms)",
			  VE2_IPC_POLL_INTERVAL_MS);
		mod_timer(&priv->poll_timer,
			  jiffies + msecs_to_jiffies(VE2_IPC_POLL_INTERVAL_MS));
	}

	/*
	 * TODO: Create mailbox with OCM resources:
	 *   xdna_mailbox_create()
	 *   chann_info.irq = priv->ipi_irq (pre-resolved from DT)
	 *   xdna_mailbox_create_channel()
	 *
	 * TODO: Call reused AIE4 helpers:
	 *   aie4_mgmt_fw_query(ndev)
	 *   aie4_mgmt_fw_init(ndev)
	 *   aie4_partition_init(ndev)
	 *   aie4_error_async_events_alloc(ndev)
	 */

	XDNA_DBG(xdna, "VE2-IPC init complete, OCM size %pa", &priv->ocm_size);
	return 0;

err_fini:
	xa_destroy(&ndev->cert_comp_xa);
	mutex_destroy(&ndev->aie4_lock);
	return ret;
}

static void ve2_ipc_fini(struct amdxdna_dev *xdna)
{
	struct amdxdna_dev_hdl *ndev = xdna->dev_handle;
	struct ve2_ipc_priv *priv;

	if (!ndev)
		return;

	priv = (struct ve2_ipc_priv *)ndev->priv_data;
	if (priv)
		timer_delete_sync(&priv->poll_timer);

	/*
	 * TODO: Tear down mailbox:
	 *   aie4_error_async_events_free(ndev)
	 *   xdna_mailbox_destroy_channel(ndev->mgmt_chann)
	 *   xdna_mailbox_destroy(ndev->mbox)
	 */

	xa_destroy(&ndev->cert_comp_xa);
	mutex_destroy(&ndev->aie4_lock);
	XDNA_DBG(xdna, "VE2-IPC fini complete");
}

static int ve2_ipc_resume(struct amdxdna_dev *xdna)
{
	/* TODO: re-init mailbox and doorbell ring */
	return 0;
}

static void ve2_ipc_suspend(struct amdxdna_dev *xdna)
{
	/* TODO: stop mailbox, disable IPI */
}

static void ve2_ipc_reset_prepare(struct amdxdna_dev *xdna)
{
	/* TODO: quiesce channels */
}

static int ve2_ipc_reset_done(struct amdxdna_dev *xdna)
{
	/* TODO: re-establish channels */
	return 0;
}

static int ve2_ipc_mmap(struct amdxdna_dev *xdna, struct vm_area_struct *vma)
{
	/* TODO: mmap doorbell or shared memory region to userspace */
	return -EOPNOTSUPP;
}

const struct amdxdna_dev_ops ve2_ipc_ops = {
	.init			= ve2_ipc_init,
	.fini			= ve2_ipc_fini,
	.resume			= ve2_ipc_resume,
	.suspend		= ve2_ipc_suspend,
	.reset_prepare		= ve2_ipc_reset_prepare,
	.reset_done		= ve2_ipc_reset_done,
	.mmap			= ve2_ipc_mmap,
	/* Reuse AIE4 backend for context and command paths */
	.ctx_init		= aie4_ctx_init,
	.ctx_fini		= aie4_ctx_fini,
	.ctx_config		= aie4_ctx_config,
	.cmd_submit		= aie4_cmd_submit,
	.cmd_wait		= aie4_cmd_wait,
	.get_aie_info		= aie4_get_info,
	.get_aie_array		= aie4_get_array,
	.set_aie_state		= aie4_set_state,
};
