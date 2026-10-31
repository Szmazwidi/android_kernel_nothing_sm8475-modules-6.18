// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/dma-direction.h>
#include <linux/iommu.h>
#include <linux/msm_dma_iommu_mapping.h>
#include <soc/qcom/secure_buffer.h>
#include <linux/mem-buf.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/qcom-dma-mapping.h>
#include <linux/version.h>
#include <linux/iosys-map.h>
#include "msm_cvp_core.h"
#include "msm_cvp_debug.h"
#include "msm_cvp_resources.h"
#include "cvp_core_hfi.h"
#include "msm_cvp_dsp.h"

/* 6.18 DMA-BUF entry points acquire the reservation lock internally. */
static void *__cvp_dma_buf_vmap(struct dma_buf *dbuf)
{
	struct iosys_map map = IOSYS_MAP_INIT_VADDR(NULL);
	int rc = dma_buf_vmap_unlocked(dbuf, &map);

	if (rc)
		return NULL;
	/* EVA dereferences normal memory: never reinterpret an I/O mapping. */
	if (map.is_iomem) {
		dma_buf_vunmap_unlocked(dbuf, &map);
		return NULL;
	}
	return map.vaddr;
}

static void __cvp_dma_buf_vunmap(struct dma_buf *dbuf, void *vaddr)
{
	struct iosys_map map = IOSYS_MAP_INIT_VADDR(vaddr);

	if (vaddr)
		dma_buf_vunmap_unlocked(dbuf, &map);
}

static int msm_dma_get_device_address(struct dma_buf *dbuf, u32 align,
	dma_addr_t *iova, u32 flags, struct msm_cvp_platform_resources *res,
	struct cvp_dma_mapping_info *mapping_info)
{
	int rc = 0;
	struct dma_buf_attachment *attach;
	struct sg_table *table = NULL;
	struct context_bank_info *cb = NULL;

	if (!dbuf || !iova || !mapping_info) {
		dprintk(CVP_ERR, "Invalid params: %pK, %pK, %pK\n",
			dbuf, iova, mapping_info);
		return -EINVAL;
	}

	if (is_iommu_present(res)) {
		cb = msm_cvp_smem_get_context_bank(res, flags);
		if (!cb) {
			dprintk(CVP_ERR,
				"%s: Failed to get context bank device\n",
				 __func__);
			rc = -EIO;
			goto mem_map_failed;
		}

		/* Prepare a dma buf for dma on the given device */
		attach = dma_buf_attach(dbuf, cb->dev);
		if (IS_ERR_OR_NULL(attach)) {
			rc = PTR_ERR(attach) ?: -ENOMEM;
			dprintk(CVP_ERR, "Failed to attach dmabuf\n");
			goto mem_buf_attach_failed;
		}

		/* Use the native exporter DMA mapping, including its full cache
		 * maintenance. The old vendor delayed-unmap/upstream-hint bits
		 * have no implementation in this native SMMU. Do not invent
		 * equivalent bits or skip CPU sync on an unsupported backend.
		 * Firmware LLCC slice ownership is managed separately.
		 */

		table = dma_buf_map_attachment_unlocked(attach, DMA_BIDIRECTIONAL);
		if (IS_ERR_OR_NULL(table)) {
			rc = PTR_ERR(table) ?: -ENOMEM;
			dprintk(CVP_ERR, "Failed to map table\n");
			goto mem_map_table_failed;
		}

		if (table->sgl) {
			if (flags & SMEM_CAMERA) {
				*iova = sg_phys(table->sgl);
			} else {
				*iova = table->sgl->dma_address;
			}
		} else {
			dprintk(CVP_ERR, "sgl is NULL\n");
			rc = -ENOMEM;
			goto mem_map_sg_failed;
		}

		mapping_info->dev = cb->dev;
		mapping_info->domain = cb->domain;
		mapping_info->table = table;
		mapping_info->attach = attach;
		mapping_info->buf = dbuf;
		mapping_info->cb_info = (void *)cb;
	} else {
		dprintk(CVP_MEM, "iommu not present, use phys mem addr\n");
	}

	return 0;
mem_map_sg_failed:
	dma_buf_unmap_attachment_unlocked(attach, table, DMA_BIDIRECTIONAL);
mem_map_table_failed:
	dma_buf_detach(dbuf, attach);
mem_buf_attach_failed:
mem_map_failed:
	return rc;
}

static int msm_dma_put_device_address(u32 flags,
	struct cvp_dma_mapping_info *mapping_info)
{
	int rc = 0;

	if (!mapping_info) {
		dprintk(CVP_WARN, "Invalid mapping_info\n");
		return -EINVAL;
	}

	if (!mapping_info->dev || !mapping_info->table ||
		!mapping_info->buf || !mapping_info->attach ||
		!mapping_info->cb_info) {
		dprintk(CVP_WARN, "Invalid params\n");
		return -EINVAL;
	}

	dma_buf_unmap_attachment_unlocked(mapping_info->attach,
		mapping_info->table, DMA_BIDIRECTIONAL);
	dma_buf_detach(mapping_info->buf, mapping_info->attach);

	mapping_info->dev = NULL;
	mapping_info->domain = NULL;
	mapping_info->table = NULL;
	mapping_info->attach = NULL;
	mapping_info->buf = NULL;
	mapping_info->cb_info = NULL;


	return rc;
}

struct dma_buf *msm_cvp_smem_get_dma_buf(int fd)
{
	struct dma_buf *dma_buf;

	dma_buf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(dma_buf)) {
		dprintk(CVP_ERR, "Failed to get dma_buf for %d, error %ld\n",
				fd, PTR_ERR(dma_buf));
		dma_buf = NULL;
	}

	return dma_buf;
}

void msm_cvp_smem_put_dma_buf(void *dma_buf)
{
	if (!dma_buf) {
		dprintk(CVP_ERR, "%s: NULL dma_buf\n", __func__);
		return;
	}

	dma_heap_buffer_free((struct dma_buf *)dma_buf);
}

int msm_cvp_map_smem(struct msm_cvp_inst *inst,
			struct msm_cvp_smem *smem,
			const char *str)
{
	int *vmid_list = NULL;
	int *perms_list = NULL;
	int nelems = 0;
	int rc = 0;

	dma_addr_t iova = 0;
	u32 temp = 0;
	u32 align = SZ_4K;
	struct dma_buf *dma_buf;

	if (!inst || !smem) {
		dprintk(CVP_ERR, "%s: Invalid params: %pK %pK\n",
				__func__, inst, smem);
		return -EINVAL;
	}

	dma_buf = smem->dma_buf;
	/*
	 * PONG CLS13: CamX hands EVA dma-bufs from heaps that are not mem_buf
	 * exporters (e.g. the upstream system heap). mem_buf has no vmperm for
	 * them and returns -EINVAL; they are plain HLOS memory, as in 5.10.
	 */
	if (IS_ERR(to_mem_buf_vmperm(dma_buf))) {
		nelems = 0;
	} else {
		rc = mem_buf_dma_buf_copy_vmperm(dma_buf,
				&vmid_list, &perms_list, &nelems);
		if (rc) {
			dprintk(CVP_ERR, "%s fail to get vmid and perms %d\n",
				__func__, rc);
			return rc;
		}
	}

	for (temp = 0; temp < nelems; temp++) {
		if (vmid_list[temp] == VMID_CP_PIXEL)
			smem->flags |= (SMEM_SECURE | SMEM_PIXEL);
		else if (vmid_list[temp] == VMID_CP_NON_PIXEL)
			smem->flags |= (SMEM_SECURE | SMEM_NON_PIXEL);
		else if (vmid_list[temp] == VMID_CP_CAMERA)
			smem->flags |= (SMEM_SECURE | SMEM_CAMERA);
	}

	rc = msm_dma_get_device_address(dma_buf, align, &iova, smem->flags,
			&(inst->core->resources), &smem->mapping_info);
	if (rc) {
		dprintk(CVP_ERR, "Failed to get device address: %d\n", rc);
		goto exit;
	}
	temp = (u32)iova;
	if ((dma_addr_t)temp != iova) {
		dprintk(CVP_ERR, "iova(%pa) truncated to %#x", &iova, temp);
		rc = -ERANGE;
		msm_dma_put_device_address(smem->flags, &smem->mapping_info);
		goto exit;
	}

	smem->size = dma_buf->size;
	smem->device_addr = (u32)iova;

	print_smem(CVP_MEM, str, inst, smem);
	goto success;
exit:
	smem->device_addr = 0x0;
success:
	kfree(vmid_list);
	kfree(perms_list);
	return rc;
}

int msm_cvp_unmap_smem(struct msm_cvp_inst *inst,
		struct msm_cvp_smem *smem,
		const char *str)
{
	int rc = 0;

	if (!smem) {
		dprintk(CVP_ERR, "%s: Invalid params: %pK\n", __func__, smem);
		rc = -EINVAL;
		goto exit;
	}

	print_smem(CVP_MEM, str, inst, smem);
	rc = msm_dma_put_device_address(smem->flags, &smem->mapping_info);
	if (rc) {
		dprintk(CVP_ERR, "Failed to put device address: %d\n", rc);
		goto exit;
	}

	smem->device_addr = 0x0;

exit:
	return rc;
}

static int alloc_dma_mem(size_t size, u32 align, int map_kernel,
	struct msm_cvp_platform_resources *res, struct msm_cvp_smem *mem)
{
	dma_addr_t iova = 0;
	int rc;
	struct dma_buf *dbuf;
	struct dma_heap *heap;
	struct mem_buf_lend_kernel_arg arg;
	int vmids[1];
	int perms[1];

	if (!res || !mem || !size || size > SIZE_MAX - (SZ_4K - 1))
		return -EINVAL;
	if (!is_iommu_present(res))
		return -ENODEV;
	if (mem->dma_buf || mem->kvaddr || mem->mapping_info.buf)
		return -EBUSY;
	if (align > U32_MAX - (SZ_4K - 1))
		return -EINVAL;
	align = ALIGN(align, SZ_4K);
	size = ALIGN(size, SZ_4K);
	/*
	 * PONG CLS13: secure (CP_PIXEL/CP_NON_PIXEL) buffers are hyp-assigned region by
	 * region; system heap pages are often order-0, and TZ rejects later assigns
	 * (-ENOMEM) after ~1300 regions, failing the EVA ARP buffers. Use the
	 * physically contiguous heap for them when it is available.
	 */
	heap = NULL;
	if (mem->flags & SMEM_SECURE)
		heap = dma_heap_find("pong,secure-contig");
	if (!heap)
		heap = dma_heap_find("qcom,system");
	if (!heap)
		return -ENODEV;
	dbuf = dma_heap_buffer_alloc(heap, size, 0, 0);
	dma_heap_put(heap);
	if (IS_ERR_OR_NULL(dbuf))
		return PTR_ERR(dbuf) ?: -ENOMEM;

	perms[0] = PERM_READ | PERM_WRITE;
	arg.nr_acl_entries = 1;
	arg.vmids = vmids;
	arg.perms = perms;
	rc = 0;
	if (mem->flags & SMEM_NON_PIXEL) {
		vmids[0] = VMID_CP_NON_PIXEL;
		rc = mem_buf_lend(dbuf, &arg);
	} else if (mem->flags & SMEM_PIXEL) {
		vmids[0] = VMID_CP_PIXEL;
		rc = mem_buf_lend(dbuf, &arg);
	}
	if (rc)
		goto fail_buffer;
	if (!gfa_cv.dmabuf_f_op)
		gfa_cv.dmabuf_f_op = (const struct file_operations *)dbuf->file->f_op;
	rc = msm_dma_get_device_address(dbuf, align, &iova, mem->flags,
		res, &mem->mapping_info);
	if (rc)
		goto fail_buffer;
	if (iova > U32_MAX) {
		rc = -ERANGE;
		goto fail_address;
	}
	if (map_kernel) {
		rc = dma_buf_begin_cpu_access(dbuf, DMA_BIDIRECTIONAL);
		if (rc)
			goto fail_address;
		mem->kvaddr = __cvp_dma_buf_vmap(dbuf);
		if (!mem->kvaddr) {
			dprintk(CVP_ERR, "Failed to map shared mem in kernel\n");
			rc = -EIO;
			dma_buf_end_cpu_access(dbuf, DMA_BIDIRECTIONAL);
			goto fail_address;
		}
	}
	/* Publish ownership only after every fallible step has succeeded. */
	mem->size = size;
	mem->dma_buf = dbuf;
	mem->device_addr = (u32)iova;
	return 0;

fail_address:
	msm_dma_put_device_address(mem->flags, &mem->mapping_info);
fail_buffer:
	dma_heap_buffer_free(dbuf);
	return rc;
}

static int free_dma_mem(struct msm_cvp_smem *mem)
{
	dprintk(CVP_MEM,
		"%s: dma_buf = %pK, device_addr = %x, size = %d, kvaddr = %pK\n",
		__func__, mem->dma_buf, mem->device_addr, mem->size, mem->kvaddr);

	if (mem->mapping_info.buf) {
		msm_dma_put_device_address(mem->flags, &mem->mapping_info);
		mem->device_addr = 0x0;
	}

	if (mem->kvaddr) {
		__cvp_dma_buf_vunmap(mem->dma_buf, mem->kvaddr);
		mem->kvaddr = NULL;
		dma_buf_end_cpu_access(mem->dma_buf, DMA_BIDIRECTIONAL);
	}

	if (mem->dma_buf) {
		dma_heap_buffer_free(mem->dma_buf);
		mem->dma_buf = NULL;
	}

	return 0;
}

int msm_cvp_smem_alloc(size_t size, u32 align, int map_kernel,
		void *res, struct msm_cvp_smem *smem)
{
	int rc = 0;

	if (!smem || !size) {
		dprintk(CVP_ERR, "%s: NULL smem or %d size\n",
			__func__, (u32)size);
		return -EINVAL;
	}

	rc = alloc_dma_mem(size, align, map_kernel,
		(struct msm_cvp_platform_resources *)res, smem);

	return rc;
}

int msm_cvp_smem_free(struct msm_cvp_smem *smem)
{
	int rc = 0;

	if (!smem) {
		dprintk(CVP_ERR, "NULL smem passed\n");
		return -EINVAL;
	}
	rc = free_dma_mem(smem);

	return rc;
};

int msm_cvp_smem_cache_operations(struct dma_buf *dbuf,
	enum smem_cache_ops cache_op, unsigned long offset, unsigned long size)
{
	int rc = 0;

	if (!dbuf) {
		dprintk(CVP_ERR, "%s: Invalid params\n", __func__);
		return -EINVAL;
	}

	switch (cache_op) {
	case SMEM_CACHE_CLEAN:
	case SMEM_CACHE_CLEAN_INVALIDATE:
		rc = dma_buf_begin_cpu_access_partial(dbuf, DMA_BIDIRECTIONAL,
				offset, size);
		if (rc)
			break;
		rc = dma_buf_end_cpu_access_partial(dbuf, DMA_BIDIRECTIONAL,
				offset, size);
		break;
	case SMEM_CACHE_INVALIDATE:
		rc = dma_buf_begin_cpu_access_partial(dbuf, DMA_TO_DEVICE,
				offset, size);
		if (rc)
			break;
		rc = dma_buf_end_cpu_access_partial(dbuf, DMA_FROM_DEVICE,
				offset, size);
		break;
	default:
		dprintk(CVP_ERR, "%s: cache (%d) operation not supported\n",
			__func__, cache_op);
		rc = -EINVAL;
		break;
	}

	return rc;
}

struct context_bank_info *msm_cvp_smem_get_context_bank(
	struct msm_cvp_platform_resources *res,
	unsigned int flags)
{
	struct context_bank_info *cb = NULL, *match = NULL;
	char *search_str;
	char *non_secure_cb = "cvp_hlos";
	char *secure_nonpixel_cb = "cvp_sec_nonpixel";
	char *secure_pixel_cb = "cvp_sec_pixel";
	bool is_secure = (flags & SMEM_SECURE) ? true : false;

	if (flags & SMEM_PIXEL)
		search_str = secure_pixel_cb;
	else if (flags & SMEM_NON_PIXEL)
		search_str = secure_nonpixel_cb;
	else if (flags & SMEM_CAMERA)
		search_str = secure_pixel_cb;
	else
		search_str = non_secure_cb;

	list_for_each_entry(cb, &res->context_banks, list) {
		if (cb->is_secure == is_secure &&
			!strcmp(search_str, cb->name)) {
			match = cb;
			break;
		}
	}

	if (!match)
		dprintk(CVP_ERR,
			"%s: cb not found for flags %x, is_secure %d\n",
			__func__, flags, is_secure);

	return match;
}

int msm_cvp_map_ipcc_regs(u32 *iova)
{
	struct context_bank_info *cb;
	struct msm_cvp_core *core;
	struct cvp_hfi_device *hfi_ops;
	struct iris_hfi_device *dev = NULL;
	phys_addr_t paddr;
	u32 size;

	core = list_first_entry(&cvp_driver->cores, struct msm_cvp_core, list);
	if (core) {
		hfi_ops = core->device;
		if (hfi_ops)
			dev = hfi_ops->hfi_device_data;
	}

	if (!dev)
		return -EINVAL;

	paddr = dev->res->ipcc_reg_base;
	size = dev->res->ipcc_reg_size;

	if (!paddr || !size)
		return -EINVAL;

	cb = msm_cvp_smem_get_context_bank(dev->res, 0);
	if (!cb) {
		dprintk(CVP_ERR, "%s: fail to get context bank\n", __func__);
		return -EINVAL;
	}
	*iova = dma_map_resource(cb->dev, paddr, size, DMA_BIDIRECTIONAL, 0);
	if (*iova == DMA_MAPPING_ERROR) {
		dprintk(CVP_WARN, "%s: fail to map IPCC regs\n", __func__);
		return -EFAULT;
	}
	return 0;
}
