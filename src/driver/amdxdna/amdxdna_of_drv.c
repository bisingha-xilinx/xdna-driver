// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <drm/drm_managed.h>

#include "amdxdna_of_drv.h"
#include "amdxdna_devel.h"

static int amdxdna_of_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct amdxdna_dev *xdna;
	int ret;

	xdna = devm_drm_dev_alloc(dev, &amdxdna_drm_drv, typeof(*xdna), ddev);
	if (IS_ERR(xdna))
		return PTR_ERR(xdna);

	xdna->dev_info = &dev_ve2_ipc_info;
	if (!xdna->dev_info->ops->init || !xdna->dev_info->ops->fini)
		return -EOPNOTSUPP;

	drmm_mutex_init(&xdna->ddev, &xdna->dev_lock);
	INIT_LIST_HEAD(&xdna->client_list);
	platform_set_drvdata(pdev, xdna);

	mutex_lock(&xdna->dev_lock);
	ret = xdna->dev_info->ops->init(xdna);
	mutex_unlock(&xdna->dev_lock);
	if (ret) {
		XDNA_ERR(xdna, "Hardware init failed, ret %d", ret);
		return ret;
	}

	ret = drm_dev_register(&xdna->ddev, 0);
	if (ret) {
		XDNA_ERR(xdna, "DRM register failed, ret %d", ret);
		goto err_fini;
	}

	if (!xdna->dev_handle) {
		XDNA_ERR(xdna, "amdxdna device handle is null");
		ret = -EINVAL;
		goto err_unregister;
	}

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret) {
		ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
		if (ret) {
			XDNA_ERR(xdna, "DMA mask set failed, ret %d", ret);
			goto err_unregister;
		}
		XDNA_WARN(xdna, "DMA configuration downgraded to 32bit Mask");
	}

	if (xdna->dev_info->ops->debugfs)
		xdna->dev_info->ops->debugfs(xdna);

	iommu_mode = AMDXDNA_IOMMU_NO_PASID;

	XDNA_DBG(xdna, "OF device %s probed", dev_name(dev));
	return 0;

err_unregister:
	drm_dev_unregister(&xdna->ddev);
err_fini:
	mutex_lock(&xdna->dev_lock);
	xdna->dev_info->ops->fini(xdna);
	mutex_unlock(&xdna->dev_lock);
	return ret;
}

static void amdxdna_of_remove(struct platform_device *pdev)
{
	struct amdxdna_dev *xdna = platform_get_drvdata(pdev);

	drm_dev_unplug(&xdna->ddev);
	mutex_lock(&xdna->dev_lock);
	xdna->dev_info->ops->fini(xdna);
	mutex_unlock(&xdna->dev_lock);
}

static const struct of_device_id amdxdna_of_match[] = {
	{ .compatible = "amd,amdxdna-ve2-ipc" },
	{ }
};
MODULE_DEVICE_TABLE(of, amdxdna_of_match);

static struct platform_driver amdxdna_of_driver = {
	.driver = {
		.name		= AMDXDNA_DRIVER_NAME,
		.of_match_table	= amdxdna_of_match,
	},
	.probe	= amdxdna_of_probe,
	.remove	= amdxdna_of_remove,
};

module_platform_driver(amdxdna_of_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("XRT Team <runtimeca39d@amd.com>");
MODULE_DESCRIPTION("amdxdna OF platform driver (VE2-IPC)");
