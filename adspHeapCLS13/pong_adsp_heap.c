// SPDX-License-Identifier: GPL-2.0
/*
 * PONG CLS10: "qcom,adsp" DMA-BUF CMA heap (+ CLS13 "pong,secure-contig", below).
 *
 * The waipio DT describes /soc/qcom,dma-heaps/qcom,adsp (CMA, sdsp_region), but
 * the 6.18 qcom_reserved_heaps (heaps269) registers only qseecom, qseecom-ta and
 * user-contig. Stock audioadsprpcd allocates its audio PD buffers from this heap
 * and loops forever without it. Allocation is the qcom_cma_heap.c path; the
 * dma-buf ops are the exported qcom_sg_buf_ops of qcom_reserved_heaps, so the
 * buffers carry mem_buf vmperm like every other qcom heap buffer.
 */

#include <linux/cma.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/dma-map-ops.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>

#include "qcom_sg_ops.h"
#include <soc/qcom/secure_buffer.h>
#include <linux/mem-buf-exporter.h>
#include <linux/mem-buf.h>

#define PONG_ADSP_HEAP_NAME "qcom,adsp"

struct pong_cma_heap {
	struct cma *cma;
	u32 max_align;
	bool uncached;
};

static void pong_cma_heap_free(struct qcom_sg_buffer *buffer)
{
	struct pong_cma_heap *cma_heap = dma_heap_get_drvdata(buffer->heap);
	unsigned long nr_pages = buffer->len >> PAGE_SHIFT;
	struct page *cma_pages = sg_page(buffer->sg_table.sgl);

	sg_free_table(&buffer->sg_table);
	cma_release(cma_heap->cma, cma_pages, nr_pages);
	kfree(buffer);
}

static struct dma_buf *pong_cma_heap_allocate(struct dma_heap *heap,
					      unsigned long len, u32 fd_flags,
					      u64 heap_flags)
{
	struct pong_cma_heap *cma_heap = dma_heap_get_drvdata(heap);
	struct qcom_sg_buffer *buffer;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct page *cma_pages;
	size_t size = PAGE_ALIGN(len);
	unsigned long nr_pages = size >> PAGE_SHIFT;
	unsigned long align = get_order(size);
	struct dma_buf *dmabuf;
	int ret = -ENOMEM;

	if (!size)
		return ERR_PTR(-EINVAL);
	if (align > cma_heap->max_align)
		align = cma_heap->max_align;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	buffer->heap = heap;
	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->lock);
	buffer->len = size;
	buffer->uncached = cma_heap->uncached;
	buffer->free = pong_cma_heap_free;

	cma_pages = cma_alloc(cma_heap->cma, nr_pages, align, false);
	if (!cma_pages)
		goto free_buf;

	if (PageHighMem(cma_pages)) {
		unsigned long nr_clear_pages = nr_pages;
		struct page *page = cma_pages;

		while (nr_clear_pages > 0) {
			void *vaddr = kmap_local_page(page);

			memset(vaddr, 0, PAGE_SIZE);
			kunmap_local(vaddr);
			if (fatal_signal_pending(current))
				goto free_cma;
			page++;
			nr_clear_pages--;
		}
	} else {
		memset(page_address(cma_pages), 0, size);
	}

	ret = sg_alloc_table(&buffer->sg_table, 1, GFP_KERNEL);
	if (ret)
		goto free_cma;
	sg_set_page(buffer->sg_table.sgl, cma_pages, size, 0);

	buffer->vmperm = mem_buf_vmperm_alloc(&buffer->sg_table);
	if (IS_ERR(buffer->vmperm)) {
		ret = PTR_ERR(buffer->vmperm);
		goto free_sgtable;
	}

	if (buffer->uncached) {
		dma_map_sgtable(dma_heap_get_dev(heap), &buffer->sg_table,
				DMA_BIDIRECTIONAL, 0);
		dma_unmap_sgtable(dma_heap_get_dev(heap), &buffer->sg_table,
				  DMA_BIDIRECTIONAL, 0);
	}

	exp_info.exp_name = dma_heap_get_name(heap);
	exp_info.size = buffer->len;
	exp_info.flags = fd_flags;
	exp_info.priv = buffer;
	dmabuf = qcom_dma_buf_export(&exp_info, &qcom_sg_buf_ops);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto vmperm_release;
	}
	return dmabuf;

vmperm_release:
	mem_buf_vmperm_release(buffer->vmperm);
free_sgtable:
	sg_free_table(&buffer->sg_table);
free_cma:
	cma_release(cma_heap->cma, cma_pages, nr_pages);
free_buf:
	kfree(buffer);
	return ERR_PTR(ret);
}

static const struct dma_heap_ops pong_cma_heap_ops = {
	.allocate = pong_cma_heap_allocate,
};

/*
 * PONG CLS13: "pong,secure-contig" heap for kernel clients that hyp-assign their
 * buffers (EVA persist/ARP buffers lent to CP_NON_PIXEL). Every scatterlist entry
 * becomes one TZ memory region; buffers built from order-0 system heap pages ran
 * TZ out of regions. Allocate physically contiguous chunks of up to 4 MiB
 * (compacting if needed) so a buffer is one or a few regions.
 */
#define PONG_CONTIG_HEAP_NAME "pong,secure-contig"
#define PONG_CONTIG_MAX_CHUNK SZ_4M
#define PONG_CONTIG_MIN_CHUNK SZ_64K
#define PONG_CONTIG_MAX_ENTS 128

static void pong_contig_heap_free(struct qcom_sg_buffer *buffer)
{
	struct scatterlist *sg;
	int i;

	for_each_sgtable_sg(&buffer->sg_table, sg, i)
		free_pages_exact(page_address(sg_page(sg)), sg->length);
	sg_free_table(&buffer->sg_table);
	kfree(buffer);
}

static struct dma_buf *pong_contig_heap_allocate(struct dma_heap *heap,
						 unsigned long len, u32 fd_flags,
						 u64 heap_flags)
{
	size_t size = PAGE_ALIGN(len), remaining = size, chunk;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct qcom_sg_buffer *buffer;
	struct scatterlist *sg;
	struct dma_buf *dmabuf;
	void **addr;
	size_t *lens;
	int n = 0, i, ret = -ENOMEM;

	if (!size)
		return ERR_PTR(-EINVAL);

	addr = kcalloc(PONG_CONTIG_MAX_ENTS, sizeof(*addr), GFP_KERNEL);
	lens = kcalloc(PONG_CONTIG_MAX_ENTS, sizeof(*lens), GFP_KERNEL);
	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!addr || !lens || !buffer)
		goto free_meta;

	while (remaining) {
		void *p = NULL;

		if (n == PONG_CONTIG_MAX_ENTS)
			goto free_chunks;
		chunk = min_t(size_t, remaining, PONG_CONTIG_MAX_CHUNK);
		for (;;) {
			p = alloc_pages_exact(chunk, GFP_KERNEL | __GFP_ZERO |
					      __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
			if (p || chunk <= PONG_CONTIG_MIN_CHUNK)
				break;
			chunk = PAGE_ALIGN(chunk / 2);
		}
		if (!p)
			goto free_chunks;
		addr[n] = p;
		lens[n++] = chunk;
		remaining -= chunk;
	}

	ret = sg_alloc_table(&buffer->sg_table, n, GFP_KERNEL);
	if (ret)
		goto free_chunks;
	for_each_sgtable_sg(&buffer->sg_table, sg, i)
		sg_set_page(sg, virt_to_page(addr[i]), lens[i], 0);

	buffer->heap = heap;
	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->lock);
	buffer->len = size;
	buffer->free = pong_contig_heap_free;

	buffer->vmperm = mem_buf_vmperm_alloc(&buffer->sg_table);
	if (IS_ERR(buffer->vmperm)) {
		ret = PTR_ERR(buffer->vmperm);
		goto free_table;
	}

	exp_info.exp_name = dma_heap_get_name(heap);
	exp_info.size = buffer->len;
	exp_info.flags = fd_flags;
	exp_info.priv = buffer;
	dmabuf = qcom_dma_buf_export(&exp_info, &qcom_sg_buf_ops);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		mem_buf_vmperm_release(buffer->vmperm);
		goto free_table;
	}
	kfree(addr);
	kfree(lens);
	return dmabuf;

free_table:
	sg_free_table(&buffer->sg_table);
free_chunks:
	for (i = 0; i < n; i++)
		free_pages_exact(addr[i], lens[i]);
free_meta:
	kfree(buffer);
	kfree(addr);
	kfree(lens);
	return ERR_PTR(ret);
}

static const struct dma_heap_ops pong_contig_heap_ops = {
	.allocate = pong_contig_heap_allocate,
};

static void pong_contig_heap_register(void)
{
	struct dma_heap_export_info exp_info = {
		.name = PONG_CONTIG_HEAP_NAME,
		.ops = &pong_contig_heap_ops,
	};
	struct dma_heap *heap = dma_heap_add(&exp_info);

	if (IS_ERR(heap))
		pr_err("PONG_CLS13 %s heap: %ld\n", PONG_CONTIG_HEAP_NAME, PTR_ERR(heap));
	else
		pr_info("PONG_CLS13 DMA-BUF Heap: Created %s\n", PONG_CONTIG_HEAP_NAME);
}

/*
 * PONG CLG34/CLG35: "qcom,secure-non-pixel" heap. msm_vidc allocates encoder internal buffers (the
 * iris2 ARP buffer among them) from this heap; without it every H.264/HEVC encode failed at
 * STREAMON (-ENOMEM, "No heap named qcom,secure-non-pixel"), so camera video recording stopped
 * at once. CLG34 hyp-assigned the pages directly and TZ refused (-EINVAL); CLG35 uses the path
 * proven for EVA since CLS13: a pong,secure-contig style buffer (few TZ regions) lent to
 * CP_NON_PIXEL with mem_buf_lend(). mem_buf reclaims it on release and keeps the pages if the
 * reclaim fails.
 */
#define PONG_NONPIXEL_HEAP_NAME "qcom,secure-non-pixel"

static struct dma_buf *pong_nonpixel_heap_allocate(struct dma_heap *heap,
						   unsigned long len, u32 fd_flags,
						   u64 heap_flags)
{
	int vmids[1] = { VMID_CP_NON_PIXEL };
	int perms[1] = { PERM_READ | PERM_WRITE };
	struct mem_buf_lend_kernel_arg arg = {
		.nr_acl_entries = 1,
		.vmids = vmids,
		.perms = perms,
	};
	struct dma_buf *dmabuf;
	int ret;

	dmabuf = pong_contig_heap_allocate(heap, len, fd_flags, heap_flags);
	if (IS_ERR(dmabuf))
		return dmabuf;
	ret = mem_buf_lend(dmabuf, &arg);
	if (ret) {
		pr_err("PONG_CLG35 %s: lend to CP_NON_PIXEL failed: %d\n",
		       PONG_NONPIXEL_HEAP_NAME, ret);
		dma_buf_put(dmabuf);
		return ERR_PTR(ret);
	}
	return dmabuf;
}

static const struct dma_heap_ops pong_nonpixel_heap_ops = {
	.allocate = pong_nonpixel_heap_allocate,
};

static void pong_nonpixel_heap_register(void)
{
	struct dma_heap_export_info exp_info = {
		.name = PONG_NONPIXEL_HEAP_NAME,
		.ops = &pong_nonpixel_heap_ops,
	};
	struct dma_heap *heap;

	heap = dma_heap_find(PONG_NONPIXEL_HEAP_NAME);
	if (heap) {
		dma_heap_put(heap);
		pr_info("PONG_CLG34 %s already provided\n", PONG_NONPIXEL_HEAP_NAME);
		return;
	}
	heap = dma_heap_add(&exp_info);
	if (IS_ERR(heap)) {
		pr_err("PONG_CLG34 %s heap: %ld\n", PONG_NONPIXEL_HEAP_NAME, PTR_ERR(heap));
		return;
	}
	pr_info("PONG_CLG34 DMA-BUF Heap: Created %s\n", PONG_NONPIXEL_HEAP_NAME);
}

static struct device_node *pong_find_heap_node(void)
{
	struct device_node *heaps, *np;
	const char *name;

	heaps = of_find_compatible_node(NULL, NULL, "qcom,dma-heaps");
	if (!heaps)
		return NULL;
	for_each_available_child_of_node(heaps, np) {
		if (!of_property_read_string(np, "qcom,dma-heap-name", &name) &&
		    !strcmp(name, PONG_ADSP_HEAP_NAME)) {
			of_node_put(heaps);
			return np;
		}
	}
	of_node_put(heaps);
	return NULL;
}

static int __init pong_adsp_heap_setup(void)
{
	struct dma_heap_export_info exp_info = {};
	struct platform_device *parent = NULL, *pdev;
	struct pong_cma_heap *cma_heap;
	struct device_node *np, *pnp;
	struct dma_heap *heap;
	u32 type, max_align;
	int ret;

	np = pong_find_heap_node();
	if (!np) {
		/* No such heap on this board (or in a VM): stay loaded and inert. */
		pr_info("PONG_CLS10 adsp heap: no %s node, nothing to register\n",
			PONG_ADSP_HEAP_NAME);
		return 0;
	}
	if (of_property_read_u32(np, "qcom,dma-heap-type", &type) || type != 2) {
		pr_err("PONG_CLS10 adsp heap: %pOF is not a CMA heap\n", np);
		ret = -EINVAL;
		goto put_np;
	}

	pnp = of_get_parent(np);
	if (pnp) {
		parent = of_find_device_by_node(pnp);
		of_node_put(pnp);
	}
	pdev = of_platform_device_create(np, NULL, parent ? &parent->dev : NULL);
	if (parent)
		put_device(&parent->dev);
	if (!pdev) {
		pr_err("PONG_CLS10 adsp heap: cannot create device for %pOF\n", np);
		ret = -EBUSY;
		goto put_np;
	}
	of_dma_configure(&pdev->dev, np, true);

	ret = of_reserved_mem_device_init_by_idx(&pdev->dev, np, 0);
	if (ret || !pdev->dev.cma_area) {
		dev_err(&pdev->dev, "PONG_CLS10 adsp heap: CMA region init failed: %d\n", ret);
		ret = ret ?: -EINVAL;
		goto unregister;
	}

	cma_heap = kzalloc(sizeof(*cma_heap), GFP_KERNEL);
	if (!cma_heap) {
		ret = -ENOMEM;
		goto release_mem;
	}
	cma_heap->cma = pdev->dev.cma_area;
	cma_heap->max_align = CONFIG_CMA_ALIGNMENT;
	if (!of_property_read_u32(np, "qcom,max-align", &max_align) && max_align)
		cma_heap->max_align = max_align;
	cma_heap->uncached = of_property_read_bool(np, "qcom,uncached-heap");

	exp_info.name = PONG_ADSP_HEAP_NAME;
	exp_info.ops = &pong_cma_heap_ops;
	exp_info.priv = cma_heap;
	heap = dma_heap_add(&exp_info);
	if (IS_ERR(heap)) {
		ret = PTR_ERR(heap);
		kfree(cma_heap);
		goto release_mem;
	}
	if (cma_heap->uncached)
		dma_coerce_mask_and_coherent(dma_heap_get_dev(heap), DMA_BIT_MASK(64));

	of_node_put(np);
	pr_info("PONG_CLS10 DMA-BUF Heap: Created %s\n", PONG_ADSP_HEAP_NAME);
	return 0;

release_mem:
	of_reserved_mem_device_release(&pdev->dev);
unregister:
	of_platform_device_destroy(&pdev->dev, NULL);
put_np:
	of_node_put(np);
	return ret;
}
static int __init pong_adsp_heap_init(void)
{
	int ret;

	pong_contig_heap_register();
	pong_nonpixel_heap_register();
	/* Heaps are registered from here on: never fail the module load. */
	ret = pong_adsp_heap_setup();
	if (ret)
		pr_err("PONG_CLS10 adsp heap setup failed: %d\n", ret);
	return 0;
}
/* No exit: a registered dma-buf heap cannot be removed. */
module_init(pong_adsp_heap_init);

MODULE_DESCRIPTION("PONG CLS10/CLS13 qcom,adsp CMA and pong,secure-contig DMA-BUF heaps");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("DMA_BUF");
MODULE_IMPORT_NS("DMA_BUF_HEAP");
