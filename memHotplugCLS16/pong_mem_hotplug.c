// SPDX-License-Identifier: GPL-2.0
/*
 * PONG CLS16: the waipio ABL boots the kernel with mem=<~7 GB> memhp_default_state=online
 * and the stock vendor mem-offline driver hot-adds the rest of DDR described in /memory.
 * This binds the same DT node ("qcom,mem-offline"), asks AOP to keep DDR self-refresh on
 * for every offline granule (stock PASR "refresh: on" message) and only then adds the DDR
 * above the end of boot System RAM (never holes below it, which hold no-map firmware
 * carveouts), one memory block at a time. Offlining/PASR power saving is not implemented:
 * the added memory stays online.
 */
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/mailbox_client.h>
#include <linux/memory.h>
#include <linux/memory_hotplug.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>

/* msm_qmp mailbox packet (include/linux/mailbox/qmp.h in the vendor tree) */
struct qmp_pkt {
	u32 size;
	void *data;
};

#define PONG_AOP_MSG_LEN 96

static bool pong_block_has_ram(phys_addr_t start, unsigned long block)
{
	return page_is_ram(PHYS_PFN(start)) ||
	       page_is_ram(PHYS_PFN(start + block / 2)) ||
	       page_is_ram(PHYS_PFN(start + block - PAGE_SIZE));
}

/* End of boot System RAM within /memory, rounded up to a memory block. */
static phys_addr_t pong_boot_ram_end(struct device_node *np, unsigned long block)
{
	phys_addr_t floor = 0;
	struct resource r;
	int i;

	for (i = 0; !of_address_to_resource(np, i, &r); i++) {
		phys_addr_t start = ALIGN_DOWN(r.start, block);
		phys_addr_t b = ALIGN(r.end + 1, block);

		while (b > start) {
			b -= block;
			if (pong_block_has_ram(b, block)) {
				floor = max(floor, b + block);
				break;
			}
		}
	}
	return floor;
}

static int pong_aop_refresh_on(struct mbox_chan *chan, phys_addr_t addr)
{
	char msg[PONG_AOP_MSG_LEN] = {};
	struct qmp_pkt pkt = { .size = PONG_AOP_MSG_LEN, .data = msg };

	snprintf(msg, sizeof(msg),
		 "{class: ddr, event: pasr, addr_hi: 0x%08x, addr_lo: 0x%08x, refresh: on}",
		 upper_32_bits(addr), lower_32_bits(addr));
	return mbox_send_message(chan, &pkt);
}

static int pong_mem_hotplug_probe(struct platform_device *pdev)
{
	unsigned long block = memory_block_size_bytes();
	struct mbox_client cl = {
		.dev = &pdev->dev,
		.tx_block = true,
		.tx_tout = 1000,
		.knows_txdone = false,
	};
	struct device_node *mem;
	struct mbox_chan *chan;
	phys_addr_t floor, a;
	u64 granule = SZ_1G, added = 0;
	u32 granule_mb;
	int i, ret;

	if (!of_property_read_u32(pdev->dev.of_node, "granule", &granule_mb) && granule_mb)
		granule = (u64)granule_mb * SZ_1M;

	mem = of_find_node_by_path("/memory");
	if (!mem)
		return dev_err_probe(&pdev->dev, -ENODEV, "PONG_CLS16 no /memory node\n");
	floor = pong_boot_ram_end(mem, block);
	if (!floor) {
		of_node_put(mem);
		return dev_err_probe(&pdev->dev, -ENODEV, "PONG_CLS16 no boot RAM in /memory\n");
	}

	chan = mbox_request_channel(&cl, 0);
	if (IS_ERR(chan)) {
		of_node_put(mem);
		return dev_err_probe(&pdev->dev, PTR_ERR(chan), "PONG_CLS16 AOP mailbox\n");
	}

	for (i = 0; ; i++) {
		struct resource r;
		phys_addr_t start, end;

		if (of_address_to_resource(mem, i, &r))
			break;
		start = max_t(phys_addr_t, ALIGN(r.start, block), floor);
		end = ALIGN_DOWN(r.end + 1, block);
		if (start >= end)
			continue;
		/* DDR must be refreshed before Linux uses it (stock sends this on online). */
		for (a = ALIGN_DOWN(start, granule); a < end; a += granule) {
			ret = pong_aop_refresh_on(chan, a);
			if (ret < 0) {
				dev_err(&pdev->dev, "PONG_CLS16 AOP refresh on %pa: %d, not adding\n", &a, ret);
				goto out;
			}
		}
		for (a = start; a < end; a += block) {
			ret = add_memory(0, a, block, MHP_NONE);
			if (!ret)
				added += block;
			else if (ret != -EEXIST)
				dev_warn(&pdev->dev, "PONG_CLS16 add %pa: %d\n", &a, ret);
		}
	}
out:
	mbox_free_channel(chan);
	of_node_put(mem);
	dev_info(&pdev->dev, "PONG_CLS16 mem: boot RAM ends %pa, hot-added %llu MiB\n",
		 &floor, added >> 20);
	return 0;
}

static const struct of_device_id pong_mem_hotplug_match[] = {
	{ .compatible = "qcom,mem-offline" },
	{}
};
MODULE_DEVICE_TABLE(of, pong_mem_hotplug_match);

static struct platform_driver pong_mem_hotplug_driver = {
	.probe = pong_mem_hotplug_probe,
	.driver = {
		.name = "pong-mem-hotplug",
		.of_match_table = pong_mem_hotplug_match,
		/* hot-added memory cannot be given back by unbinding */
		.suppress_bind_attrs = true,
	},
};
/* Hot-added memory stays; no module exit. */
static int __init pong_mem_hotplug_init(void)
{
	return platform_driver_register(&pong_mem_hotplug_driver);
}
module_init(pong_mem_hotplug_init);

MODULE_DESCRIPTION("PONG CLS16 waipio DDR hot-add above mem= (mem-offline DT node)");
MODULE_LICENSE("GPL");
