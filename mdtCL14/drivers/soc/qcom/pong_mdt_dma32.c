// SPDX-License-Identifier: GPL-2.0-only
/*
 * PONG CL14: 32-bit DMA helper device for PAS metadata (see mdt_loader.c).
 * Separate unit: its includes must not reach the exported mdt_loader types.
 */
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/platform_device.h>

struct device *pong_mdt_dma32_dev(void);

static struct platform_device *mdt_dma32_pdev;

struct device *pong_mdt_dma32_dev(void)
{
	return mdt_dma32_pdev ? &mdt_dma32_pdev->dev : NULL;
}

static int __init mdt_loader_init(void)
{
	struct platform_device_info info = {
		.name = "pong-mdt-dma32",
		.id = PLATFORM_DEVID_NONE,
		.dma_mask = DMA_BIT_MASK(32),
	};
	struct platform_device *pdev = platform_device_register_full(&info);

	/* Without the helper device the original allocation path is used. */
	if (IS_ERR(pdev))
		pr_warn("PONG_CL14 dma32 helper device: %ld\n", PTR_ERR(pdev));
	else
		mdt_dma32_pdev = pdev;
	return 0;
}
module_init(mdt_loader_init);
