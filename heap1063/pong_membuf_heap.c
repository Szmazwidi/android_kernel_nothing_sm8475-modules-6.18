// SPDX-License-Identifier: GPL-2.0
/*
 * DMABUF System heap exporter
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019, 2020 Linaro Ltd.
 *
 * Portions based off of Andrew Davis' SRAM heap:
 * Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 */

#include <linux/dma-buf.h>
#include <linux/iosys-map.h>
#include <linux/limits.h>
#include <linux/mem-buf-exporter.h>
#include <linux/msm_dma_iommu_mapping.h>
#include <linux/qcom-dma-mapping.h>
#include <linux/dma-direct.h>
#include <linux/dma-mapping.h>
#include <linux/dma-heap.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/iommu.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/scatterlist.h>
#include <linux/swiotlb.h>
#include <linux/vmalloc.h>

struct system_heap_buffer {
	struct dma_heap *heap;
	struct mem_buf_vmperm *vmperm;
	struct list_head attachments;
	struct mutex lock;
	unsigned long len;
	struct sg_table sg_table;
	int vmap_cnt;
	void *vaddr;

	bool uncached;
};

struct dma_heap_attachment {
	struct device *dev;
	struct sg_table table;
	struct list_head list;
	bool mapped;

	bool uncached;
};

#define LOW_ORDER_GFP (GFP_HIGHUSER | __GFP_ZERO)
#define HIGH_ORDER_GFP  (((GFP_HIGHUSER | __GFP_ZERO | __GFP_NOWARN \
				| __GFP_NORETRY) & ~__GFP_RECLAIM) \
				| __GFP_COMP)
static gfp_t order_flags[] = {HIGH_ORDER_GFP, HIGH_ORDER_GFP, LOW_ORDER_GFP};
/*
 * The selection of the orders used for allocation (1MB, 64K, 4K) is designed
 * to match with the sizes often found in IOMMUs. Using order 4 pages instead
 * of order 0 pages can significantly improve the performance of many IOMMUs
 * by reducing TLB pressure and time spent updating page tables.
 *
 * Note: When the order is 0, the minimum allocation is PAGE_SIZE. The possible
 * page sizes for ARM devices could be 4K, 16K and 64K.
 */
#define ORDER_1M (20 - PAGE_SHIFT)
#define ORDER_64K (16 - PAGE_SHIFT)
#define ORDER_FOR_PAGE_SIZE (0)
static const unsigned int orders[] = {ORDER_1M, ORDER_64K, ORDER_FOR_PAGE_SIZE};

#define NUM_ORDERS ARRAY_SIZE(orders)

static int dup_sg_table(struct sg_table *from, struct sg_table *to)
{
	struct scatterlist *sg, *new_sg;
	int ret, i;

	ret = sg_alloc_table(to, from->orig_nents, GFP_KERNEL);
	if (ret)
		return ret;

	new_sg = to->sgl;
	for_each_sgtable_sg(from, sg, i) {
		sg_set_page(new_sg, sg_page(sg), sg->length, sg->offset);
		new_sg = sg_next(new_sg);
	}

	return 0;
}

static int system_heap_attach(struct dma_buf *dmabuf,
			      struct dma_buf_attachment *attachment)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;
	int ret;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	ret = dup_sg_table(&buffer->sg_table, &a->table);
	if (ret) {
		kfree(a);
		return ret;
	}

	a->dev = attachment->dev;
	INIT_LIST_HEAD(&a->list);
	a->mapped = false;
	a->uncached = buffer->uncached;
	attachment->priv = a;

	mutex_lock(&buffer->lock);
	list_add(&a->list, &buffer->attachments);
	mutex_unlock(&buffer->lock);

	return 0;
}

static void system_heap_detach(struct dma_buf *dmabuf,
			       struct dma_buf_attachment *attachment)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a = attachment->priv;

	mutex_lock(&buffer->lock);
	list_del(&a->list);
	mutex_unlock(&buffer->lock);

	sg_free_table(&a->table);
	kfree(a);
}

static struct sg_table *system_heap_map_dma_buf(struct dma_buf_attachment *attachment,
				     enum dma_data_direction direction)
{
	struct dma_heap_attachment *a = attachment->priv;
	struct sg_table *table = &a->table;
	struct system_heap_buffer *buffer;
	struct mem_buf_vmperm *vmperm;
	unsigned long attrs = attachment->dma_map_attrs;
	int ret;

	buffer = attachment->dmabuf->priv;
	vmperm = buffer->vmperm;

	if (attrs & DMA_ATTR_QTI_SMMU_PROXY_MAP)
		return ERR_PTR(-EOPNOTSUPP);

	/* Prevent map/unmap during begin/end_cpu_access */
	mutex_lock(&buffer->lock);

	/* Ensure VM permissions are constant while the buffer is mapped */
	mem_buf_vmperm_pin(vmperm);
	if (buffer->uncached || !mem_buf_vmperm_can_cmo(vmperm))
		attrs |= DMA_ATTR_SKIP_CPU_SYNC;

	if (attrs & DMA_ATTR_DELAYED_UNMAP) {
		ret = msm_dma_map_sgtable(attachment->dev, table, direction,
					  attachment->dmabuf, attrs);
	} else if (!a->mapped) {
		ret = dma_map_sgtable(attachment->dev, table, direction, attrs);
	} else {
		dev_err(attachment->dev, "Error: Dma-buf is already mapped!\n");
		ret = -EBUSY;
	}

	if (ret) {
		table = ERR_PTR(ret);
		goto err_map_sgtable;
	}

	a->mapped = true;
	mutex_unlock(&buffer->lock);
	return table;

err_map_sgtable:
	mem_buf_vmperm_unpin(vmperm);
	mutex_unlock(&buffer->lock);
	return table;
}

static void system_heap_unmap_dma_buf(struct dma_buf_attachment *attachment,
			   struct sg_table *table,
			   enum dma_data_direction direction)
{
	struct dma_heap_attachment *a = attachment->priv;
	struct system_heap_buffer *buffer;
	struct mem_buf_vmperm *vmperm;
	unsigned long attrs = attachment->dma_map_attrs;

	buffer = attachment->dmabuf->priv;
	vmperm = buffer->vmperm;

	if (WARN_ON_ONCE(attrs & DMA_ATTR_QTI_SMMU_PROXY_MAP))
		return;

	/* Prevent map/unmap during begin/end_cpu_access */
	mutex_lock(&buffer->lock);

	if (buffer->uncached || !mem_buf_vmperm_can_cmo(vmperm))
		attrs |= DMA_ATTR_SKIP_CPU_SYNC;

	a->mapped = false;

	if (attrs & DMA_ATTR_DELAYED_UNMAP) {
		msm_dma_unmap_sgtable(attachment->dev, table, direction,
				      attachment->dmabuf, attrs);
	} else {
		dma_unmap_sgtable(attachment->dev, table, direction, attrs);
	}
	mem_buf_vmperm_unpin(vmperm);
	mutex_unlock(&buffer->lock);
}

static int system_heap_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
						enum dma_data_direction direction)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;

	mutex_lock(&buffer->lock);
	/* Pin ownership for this cache-maintenance operation before checking it. */
	mem_buf_vmperm_pin(buffer->vmperm);
 if (!mem_buf_vmperm_can_cmo(buffer->vmperm)) {
  mem_buf_vmperm_unpin(buffer->vmperm);
  mutex_unlock(&buffer->lock);
  return -EPERM;
 }



	if (buffer->vmap_cnt)
		invalidate_kernel_vmap_range(buffer->vaddr, buffer->len);
	if (!buffer->uncached) {
		list_for_each_entry(a, &buffer->attachments, list) {
			if (!a->mapped)
				continue;
			dma_sync_sgtable_for_cpu(a->dev, &a->table, direction);
		}
	}
	mem_buf_vmperm_unpin(buffer->vmperm);
	mutex_unlock(&buffer->lock);

	return 0;
}

static int system_heap_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
					      enum dma_data_direction direction)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;

	mutex_lock(&buffer->lock);
	/* Pin ownership for this cache-maintenance operation before checking it. */
	mem_buf_vmperm_pin(buffer->vmperm);
 if (!mem_buf_vmperm_can_cmo(buffer->vmperm)) {
  mem_buf_vmperm_unpin(buffer->vmperm);
  mutex_unlock(&buffer->lock);
  return -EPERM;
 }



	if (buffer->vmap_cnt)
		flush_kernel_vmap_range(buffer->vaddr, buffer->len);
	if (!buffer->uncached) {
		list_for_each_entry(a, &buffer->attachments, list) {
			if (!a->mapped)
				continue;
			dma_sync_sgtable_for_device(a->dev, &a->table, direction);
		}
	}
	mem_buf_vmperm_unpin(buffer->vmperm);
	mutex_unlock(&buffer->lock);

	return 0;
}


/* CPU mappings retain real mem-buf ownership for their full lifetime. */
static void *system_heap_do_vmap(struct system_heap_buffer *buffer)
{
	struct sg_page_iter iter;
	unsigned long count = buffer->len >> PAGE_SHIFT;
	struct page **pages;
	unsigned long used = 0;
	pgprot_t prot = buffer->uncached ? pgprot_writecombine(PAGE_KERNEL) : PAGE_KERNEL;
	void *addr;

	if (!count || count > UINT_MAX || (buffer->len & (PAGE_SIZE - 1)))
		return ERR_PTR(-EINVAL);
	pages = kvmalloc_array(count, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return ERR_PTR(-ENOMEM);
	for_each_sgtable_page(&buffer->sg_table, &iter, 0) {
		if (used == count) {
			kvfree(pages);
			return ERR_PTR(-EINVAL);
		}
		pages[used++] = sg_page_iter_page(&iter);
	}
	if (used != count) {
		kvfree(pages);
		return ERR_PTR(-EINVAL);
	}
	addr = vmap(pages, count, VM_MAP, prot);
	kvfree(pages);
	return addr ? addr : ERR_PTR(-ENOMEM);
}

static int system_heap_vmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	void *addr;
	int rc = 0;

	mutex_lock(&buffer->lock);
	if (buffer->vmap_cnt == INT_MAX) {
		rc = -EOVERFLOW;
		goto out;
	}
	if (!buffer->vmap_cnt)
		mem_buf_vmperm_pin(buffer->vmperm);
	if (!mem_buf_vmperm_can_vmap(buffer->vmperm)) {
		rc = -EPERM;
		goto unpin_failed_first;
	}
	if (buffer->vmap_cnt) {
		buffer->vmap_cnt++;
		iosys_map_set_vaddr(map, buffer->vaddr);
		goto out;
	}
	addr = system_heap_do_vmap(buffer);
	if (IS_ERR(addr)) {
		rc = PTR_ERR(addr);
		goto unpin_failed_first;
	}
	buffer->vaddr = addr;
	buffer->vmap_cnt = 1;
	iosys_map_set_vaddr(map, addr);
	goto out;
unpin_failed_first:
	if (!buffer->vmap_cnt)
		mem_buf_vmperm_unpin(buffer->vmperm);
out:
	mutex_unlock(&buffer->lock);
	return rc;
}

static void system_heap_vunmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct system_heap_buffer *buffer = dmabuf->priv;

	mutex_lock(&buffer->lock);
	if (WARN_ON_ONCE(!buffer->vmap_cnt || map->is_iomem ||
			 map->vaddr != buffer->vaddr)) {
		mutex_unlock(&buffer->lock);
		return;
	}
	if (!--buffer->vmap_cnt) {
		vunmap(buffer->vaddr);
		buffer->vaddr = NULL;
		mem_buf_vmperm_unpin(buffer->vmperm);
	}
	mutex_unlock(&buffer->lock);
	iosys_map_clear(map);
}

static void system_heap_dma_buf_release(struct dma_buf *dmabuf)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct sg_table *table;
	struct scatterlist *sg;
	int i;

	/* A live CPU mapping must never become freed physical pages. */
	if (WARN_ON_ONCE(buffer->vmap_cnt))
		return;
	/* A failed reclaim leaves physical ownership unknown: retain pages. */
 if (mem_buf_vmperm_release(buffer->vmperm)) {
  pr_err("pong-system: VM permission release failed; retaining pages\n");
  return;
 }
	msm_dma_buf_freed(buffer);
	table = &buffer->sg_table;
	for_each_sgtable_sg(table, sg, i) {
		struct page *page = sg_page(sg);

		__free_pages(page, compound_order(page));
	}
	sg_free_table(table);
	kfree(buffer);
}

/* Based on actual Pong66 qcom_sg_vm_ops/qcom_sg_mmap. Keep a permission
 * pin per VMA (including fork/split), validate bounds, and unwind any failed
 * remap before publishing vm_ops. SG pages stay owned by the dma-buf file.
 */
static void pong104_vma_open(struct vm_area_struct *vma)
{
 mem_buf_vmperm_pin(vma->vm_private_data);
}
static void pong104_vma_close(struct vm_area_struct *vma)
{
 mem_buf_vmperm_unpin(vma->vm_private_data);
}
static const struct vm_operations_struct pong104_vm_ops = {
 .open = pong104_vma_open,
 .close = pong104_vma_close,
};
static int pong104_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
 struct system_heap_buffer *buffer = dmabuf->priv;
 struct sg_table *table = &buffer->sg_table;
 struct scatterlist *sg;
 unsigned long addr = vma->vm_start;
 unsigned long bytes = vma->vm_end - vma->vm_start;
 unsigned long offset;
 int i, ret;

 if (vma->vm_end <= vma->vm_start ||
     vma->vm_pgoff > (buffer->len >> PAGE_SHIFT))
  return -EINVAL;
 offset = vma->vm_pgoff << PAGE_SHIFT;
 if (bytes > buffer->len - offset)
  return -EINVAL;
 mem_buf_vmperm_pin(buffer->vmperm);
 if (!mem_buf_vmperm_can_mmap(buffer->vmperm, vma)) {
  ret = -EPERM;
  goto unpin;
 }
 if (buffer->uncached)
  vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
 for_each_sgtable_sg(table, sg, i) {
  struct page *page = sg_page(sg);
  unsigned long len = sg->length;
  if (offset >= len) {
   offset -= len;
   continue;
  }
  page += offset >> PAGE_SHIFT;
  len -= offset;
  offset = 0;
  len = min(len, vma->vm_end - addr);
  ret = remap_pfn_range(vma, addr, page_to_pfn(page), len,
                        vma->vm_page_prot);
  if (ret)
   goto unpin;
  addr += len;
  if (addr == vma->vm_end) {
   vma->vm_private_data = buffer->vmperm;
   vma->vm_ops = &pong104_vm_ops;
   return 0;
  }
 }
 ret = -EINVAL;
unpin:
 mem_buf_vmperm_unpin(buffer->vmperm);
 return ret;
}

static struct mem_buf_vmperm *system_heap_lookup(struct dma_buf *dmabuf)
{
 struct system_heap_buffer *buffer = dmabuf->priv;
 return buffer->vmperm;
}
static struct mem_buf_dma_buf_ops system_heap_buf_ops = {
 .lookup = system_heap_lookup,
 .attach = system_heap_attach,
 .dma_ops = {
	.detach = system_heap_detach,
	.map_dma_buf = system_heap_map_dma_buf,
	.unmap_dma_buf = system_heap_unmap_dma_buf,
	.begin_cpu_access = system_heap_dma_buf_begin_cpu_access,
	.end_cpu_access = system_heap_dma_buf_end_cpu_access,
	.release = system_heap_dma_buf_release,
	.vmap = system_heap_vmap,
	.vunmap = system_heap_vunmap,
	.mmap = pong104_mmap,
 },
};

static struct page *alloc_largest_available(unsigned long size,
					    unsigned int max_order)
{
	struct page *page;
	int i;

	for (i = 0; i < NUM_ORDERS; i++) {
		if (size < (PAGE_SIZE << orders[i]))
			continue;
		if (max_order < orders[i])
			continue;

		page = alloc_pages(order_flags[i], orders[i]);
		if (!page)
			continue;
		return page;
	}
	return NULL;
}

static struct dma_buf *system_heap_do_allocate(struct dma_heap *heap,
					       unsigned long len,
					       u32 fd_flags,
					       u64 heap_flags,
					       bool uncached)
{
	struct system_heap_buffer *buffer;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	unsigned long size_remaining = len;
	unsigned int max_order = orders[0];
	struct dma_buf *dmabuf;
	struct sg_table *table;
	struct scatterlist *sg;
	struct list_head pages;
	struct page *page, *tmp_page;
	int i, ret = -ENOMEM;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->lock);
	buffer->heap = heap;
	buffer->len = len;
	buffer->uncached = uncached;

	INIT_LIST_HEAD(&pages);
	i = 0;
	while (size_remaining > 0) {
		/*
		 * Avoid trying to allocate memory if the process
		 * has been killed by SIGKILL
		 */
		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			goto free_buffer;
		}

		page = alloc_largest_available(size_remaining, max_order);
		if (!page)
			goto free_buffer;

		list_add_tail(&page->lru, &pages);
		size_remaining -= page_size(page);
		max_order = compound_order(page);
		i++;
	}

	table = &buffer->sg_table;
	if (sg_alloc_table(table, i, GFP_KERNEL))
		goto free_buffer;

	sg = table->sgl;
	list_for_each_entry_safe(page, tmp_page, &pages, lru) {
		sg_set_page(sg, page, page_size(page), 0);
		sg = sg_next(sg);
		list_del(&page->lru);
	}

 buffer->vmperm = mem_buf_vmperm_alloc(table);
 if (IS_ERR(buffer->vmperm)) {
  ret = PTR_ERR(buffer->vmperm);
  goto free_pages;
 }
	/* create the dmabuf */
	exp_info.exp_name = dma_heap_get_name(heap);
	exp_info.ops = &system_heap_buf_ops.dma_ops;
	exp_info.size = buffer->len;
	exp_info.flags = fd_flags;
	exp_info.priv = buffer;
	dmabuf = mem_buf_dma_buf_export(&exp_info, &system_heap_buf_ops);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		mem_buf_vmperm_release(buffer->vmperm);
		goto free_pages;
	}

	/*
	 * For uncached buffers, we need to initially flush cpu cache, since
	 * the __GFP_ZERO on the allocation means the zeroing was done by the
	 * cpu and thus it is likely cached. Map (and implicitly flush) and
	 * unmap it now so we don't get corruption later on.
	 */
	if (buffer->uncached) {
		dma_map_sgtable(dma_heap_get_dev(heap), table, DMA_BIDIRECTIONAL, 0);
		dma_unmap_sgtable(dma_heap_get_dev(heap), table, DMA_BIDIRECTIONAL, 0);
	}

	pr_info_once("PONG102_MEMBUF_EXPORT bytes=%lu vmperm=currentVM nativeSG\n", len);
	return dmabuf;

free_pages:
	for_each_sgtable_sg(table, sg, i) {
		struct page *p = sg_page(sg);

		__free_pages(p, compound_order(p));
	}
	sg_free_table(table);
free_buffer:
	list_for_each_entry_safe(page, tmp_page, &pages, lru)
		__free_pages(page, compound_order(page));
	kfree(buffer);

	return ERR_PTR(ret);
}

static struct dma_buf *system_heap_allocate(struct dma_heap *heap,
					    unsigned long len,
					    u32 fd_flags,
					    u64 heap_flags)
{
	return system_heap_do_allocate(heap, len, fd_flags, heap_flags, false);
}

static const struct dma_heap_ops system_heap_ops = {
	.allocate = system_heap_allocate,
};

static int __init system_heap_create(void)
{
 struct dma_heap_export_info info = {
  .name = "qcom,system", .ops = &system_heap_ops,
 };
 struct dma_heap *heap = dma_heap_add(&info);
 return PTR_ERR_OR_ZERO(heap);
}
module_init(system_heap_create);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Native cached pages with real Qualcomm MEMBUF vmperm; Pong diagnostic heap");
MODULE_IMPORT_NS("DMA_BUF");
MODULE_IMPORT_NS("DMA_BUF_HEAP");
