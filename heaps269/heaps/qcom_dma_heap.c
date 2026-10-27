// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/err.h>

#include <linux/qcom_dma_heap.h>
#include <linux/qcom_tvm_heap.h>
#include "qcom_cma_heap.h"
#include "qcom_dt_parser.h"
#include "qcom_carveout_heap.h"
#include "qcom_dma_heap_priv.h"

/*
 * We cache the file ops used by DMA-BUFs so that a user with a struct file
 * pointer can determine if that file is for a DMA-BUF. We can't initialize
 * the pointer here at compile time as ERR_PTR() evaluates to a function.
 */
#define FOPS_INIT_VAL ERR_PTR(-EINVAL)
static const struct file_operations *dma_buf_cached_fops;

static int qcom_dma_heap_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i;
	struct platform_data *heaps;

	/* Existing proven system/MEMBUF heaps stay registered by their provider. */

	heaps = parse_heap_dt(pdev);
	if (IS_ERR_OR_NULL(heaps))
		return PTR_ERR(heaps);

	for (i = 0; i < heaps->nr; i++) {
		struct platform_heap *heap_data = &heaps->heaps[i];

		if (strcmp(heap_data->name, "qcom,qseecom") &&
		    strcmp(heap_data->name, "qcom,qseecom-ta") &&
		    strcmp(heap_data->name, "qcom,user-contig"))
			continue;
		if (heap_data->type != HEAP_TYPE_CMA) {
			pr_err("PONG269 unexpected secure heap type for %s\n", heap_data->name);
			ret = -EINVAL;
			break;
		}
		switch (heap_data->type) {
		case HEAP_TYPE_SECURE_CARVEOUT:
			ret = qcom_secure_carveout_heap_create(heap_data);
			break;
		case HEAP_TYPE_CARVEOUT:
			ret = qcom_carveout_heap_create(heap_data);
			break;
		case HEAP_TYPE_CMA:
			ret = qcom_add_cma_heap(heap_data);
			break;
		case HEAP_TYPE_TVM_CARVEOUT:
			ret = qcom_tvm_carveout_heap_create(heap_data);
			break;
		default:
			pr_err("%s: Unknown heap type %u\n", __func__, heap_data->type);
			break;
		}

		if (ret)
			pr_err("%s: DMA-BUF Heap: Failed to create %s, error is %d\n",
			       __func__, heap_data->name, ret);
		else
			pr_info("%s: DMA-BUF Heap: Created %s\n", __func__,
				heap_data->name);
	}

	free_pdata(heaps);

	dma_buf_cached_fops = FOPS_INIT_VAL;

	return ret;
}

void qcom_store_dma_buf_fops(struct file *file)
{
	if (dma_buf_cached_fops == FOPS_INIT_VAL)
		dma_buf_cached_fops = file->f_op;
}
EXPORT_SYMBOL(qcom_store_dma_buf_fops);

bool qcom_is_dma_buf_file(struct file *file)
{
	return file->f_op == dma_buf_cached_fops;
}
EXPORT_SYMBOL(qcom_is_dma_buf_file);

static const struct of_device_id qcom_dma_heap_match_table[] = {
	{.compatible = "qcom,dma-heaps"},
	{},
};

static struct platform_driver qcom_dma_heap_driver = {
	.probe = qcom_dma_heap_probe,
	.driver = {
		.name = "qcom-dma-heap",
		.of_match_table = qcom_dma_heap_match_table,
	},
};

static int __init init_heap_driver(void)
{
	return platform_driver_register(&qcom_dma_heap_driver);
}
module_init(init_heap_driver);

MODULE_LICENSE("GPL");

MODULE_DESCRIPTION("Pong reserved QSEE DMA heaps from Qualcomm 6.6");
MODULE_IMPORT_NS("DMA_BUF");
MODULE_IMPORT_NS("DMA_BUF_HEAP");
