// SPDX-License-Identifier: GPL-2.0-only
/*
 * Nothing Phone (2) PowerStats nodes for the stock PowerStats HAL.
 *
 * The stock HAL (and its vendor sepolicy, genfscon sysfs_powerstats) reads
 *   /sys/module/soc_sleep_stats/soc_sleep/nt_rpmh_stats
 *   /sys/module/soc_sleep_stats/rpmh_stats/nt_master_stats
 * which on 5.10 came from the vendor soc_sleep_stats module. The node format
 * and the read logic come from redducc's qcom_stats change for 6.12
 * (redducc-stuff/kernel_nothing_pong c61396ae38). On 6.18 the debugfs stats
 * are served by upstream qcom_stats, so this module only provides the two
 * sysfs nodes, under the module name the stock policy labels.
 */

#include <linux/io.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/soc/qcom/smem.h>
#include <linux/sysfs.h>
#include <asm/arch_timer.h>

#define RPMH_STATS_OFFSET	0x48	/* upstream qcom_stats rpmh_data */
#define NUM_RECORDS		2
#define COUNT_OFFSET		0x4
#define ACCUMULATED_OFFSET	0x18
#define RECORD_SIZE		0x18	/* struct sleep_stats */

struct sleep_stats {
	u32 stat_type;
	u32 count;
	u64 last_entered_at;
	u64 last_exited_at;
	u64 accumulated;
};

struct subsystem_data {
	const char *name;
	u32 smem_item;
	u32 pid;
};

static const struct subsystem_data subsystems[] = {
	{ "modem", 605, 1 },
	{ "wpss", 605, 13 },
	{ "adsp", 606, 2 },
	{ "cdsp", 607, 5 },
	{ "slpi", 608, 3 },
	{ "gpu", 609, 0 },
	{ "display", 610, 0 },
	{ "adsp_island", 613, 2 },
	{ "slpi_island", 613, 3 },
	{ "apss", 631, QCOM_SMEM_HOST_ANY },
};

static void __iomem *nt_base;
static struct device_node *nt_node;
static struct kobject *nt_soc_sleep_kobj, *nt_rpmh_stats_kobj;

static u64 nt_ticks_to_ms(u64 ticks)
{
	do_div(ticks, 19200000 / MSEC_PER_SEC);
	return ticks;
}

static ssize_t nt_rpmh_stats_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	static const char * const fmt[] = { "vlow:%x:%llx\n", "vmin:%x:%llx\r\n" };
	ssize_t len = 0;
	int i;

	for (i = 0; i < NUM_RECORDS; i++) {
		void __iomem *base = nt_base + i * RECORD_SIZE;
		u32 count = readl(base + COUNT_OFFSET);
		u64 accumulated = readq(base + ACCUMULATED_OFFSET);

		len += sysfs_emit_at(buf, len, fmt[i], count, nt_ticks_to_ms(accumulated));
	}

	return len;
}

static ssize_t nt_master_stats_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	ssize_t len = 0;
	const char *name;
	int i, j, n;

	n = of_property_count_strings(nt_node, "ss-name");
	for (i = 0; i < n; i++) {
		if (of_property_read_string_index(nt_node, "ss-name", i, &name))
			continue;
		for (j = 0; j < ARRAY_SIZE(subsystems); j++) {
			struct sleep_stats *stat;
			u64 accumulated;

			if (strcmp(subsystems[j].name, name))
				continue;
			stat = qcom_smem_get(subsystems[j].pid, subsystems[j].smem_item, NULL);
			if (IS_ERR(stat))
				break;
			accumulated = stat->accumulated;
			if (stat->last_entered_at > stat->last_exited_at)
				accumulated += arch_timer_read_counter() - stat->last_entered_at;
			len += sysfs_emit_at(buf, len, "%s:%x:%llx\n", name, stat->count,
					     nt_ticks_to_ms(accumulated));
			break;
		}
	}

	return len;
}

static struct kobj_attribute nt_rpmh_stats_attr = __ATTR_RO(nt_rpmh_stats);
static struct kobj_attribute nt_master_stats_attr = __ATTR_RO(nt_master_stats);

static int __init nt_soc_sleep_stats_init(void)
{
	struct kobject *mod_kobj = &THIS_MODULE->mkobj.kobj;
	int ret = -ENOMEM;

	nt_node = of_find_compatible_node(NULL, NULL, "qcom,rpmh-sleep-stats");
	if (!nt_node)
		nt_node = of_find_compatible_node(NULL, NULL, "qcom,rpmh-stats");
	if (!nt_node)
		return -ENODEV;

	/* read-only view of AOP MSG RAM; qcom_stats owns the device */
	nt_base = of_iomap(nt_node, 0);
	if (!nt_base) {
		of_node_put(nt_node);
		return -ENOMEM;
	}
	nt_base += RPMH_STATS_OFFSET;

	nt_soc_sleep_kobj = kobject_create_and_add("soc_sleep", mod_kobj);
	nt_rpmh_stats_kobj = kobject_create_and_add("rpmh_stats", mod_kobj);
	if (!nt_soc_sleep_kobj || !nt_rpmh_stats_kobj)
		goto err;
	ret = sysfs_create_file(nt_soc_sleep_kobj, &nt_rpmh_stats_attr.attr);
	if (!ret)
		ret = sysfs_create_file(nt_rpmh_stats_kobj, &nt_master_stats_attr.attr);
	if (ret)
		goto err;
	return 0;
err:
	kobject_put(nt_rpmh_stats_kobj);
	kobject_put(nt_soc_sleep_kobj);
	iounmap(nt_base - RPMH_STATS_OFFSET);
	of_node_put(nt_node);
	return ret;
}
module_init(nt_soc_sleep_stats_init);

static void __exit nt_soc_sleep_stats_exit(void)
{
	kobject_put(nt_rpmh_stats_kobj);
	kobject_put(nt_soc_sleep_kobj);
	iounmap(nt_base - RPMH_STATS_OFFSET);
	of_node_put(nt_node);
}
module_exit(nt_soc_sleep_stats_exit);

MODULE_DESCRIPTION("Nothing Phone (2) PowerStats sysfs nodes");
MODULE_LICENSE("GPL");
