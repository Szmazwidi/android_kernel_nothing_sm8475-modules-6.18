// SPDX-License-Identifier: GPL-2.0-only
/*
 * CLG8: "qcom,thermal-pause" cooling devices for ACK 6.18 without WALT.
 *
 * The 5.10 driver paused CPUs through walt_pause_cpus(), which does not exist on 6.18
 * (no WALT). This replacement registers the same DT cooling devices (same cdev-alias
 * names, max state 1) and isolates the CPUs with CPU hotplug instead, like the 5.10
 * qcom,cpu-hotplug cooling driver. CPU0 is never taken offline. Only CPUs taken offline
 * by this driver are brought back online.
 */
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>

struct pause_cdev {
	struct list_head node;
	struct thermal_cooling_device *cdev;
	struct device_node *np;
	cpumask_t cpus;
	cpumask_t offlined;
	unsigned long state;
	unsigned long target;
	struct work_struct work;
	char name[THERMAL_NAME_LENGTH];
};

static LIST_HEAD(pause_list);
static DEFINE_MUTEX(pause_lock);

static void pause_work(struct work_struct *work)
{
	struct pause_cdev *pc = container_of(work, struct pause_cdev, work);
	int cpu, ret;

	mutex_lock(&pause_lock);
	if (pc->target == pc->state)
		goto out;
	if (pc->target) {
		for_each_cpu(cpu, &pc->cpus) {
			if (cpu == 0 || !cpu_online(cpu) || num_online_cpus() <= 1)
				continue;
			ret = remove_cpu(cpu);
			if (!ret)
				cpumask_set_cpu(cpu, &pc->offlined);
			else
				pr_err("%s: cpu%d offline failed %d\n", pc->name, cpu, ret);
		}
	} else {
		for_each_cpu(cpu, &pc->offlined) {
			ret = add_cpu(cpu);
			if (ret)
				pr_err("%s: cpu%d online failed %d\n", pc->name, cpu, ret);
		}
		cpumask_clear(&pc->offlined);
	}
	pc->state = pc->target;
out:
	mutex_unlock(&pause_lock);
}

static int pause_get_max_state(struct thermal_cooling_device *cdev, unsigned long *state)
{
	*state = 1;
	return 0;
}

static int pause_get_cur_state(struct thermal_cooling_device *cdev, unsigned long *state)
{
	struct pause_cdev *pc = cdev->devdata;

	*state = pc->state;
	return 0;
}

static int pause_set_cur_state(struct thermal_cooling_device *cdev, unsigned long state)
{
	struct pause_cdev *pc = cdev->devdata;

	if (state > 1)
		return -EINVAL;
	pc->target = state;
	queue_work(system_highpri_wq, &pc->work);
	return 0;
}

static const struct thermal_cooling_device_ops pause_ops = {
	.get_max_state = pause_get_max_state,
	.get_cur_state = pause_get_cur_state,
	.set_cur_state = pause_set_cur_state,
};

static int thermal_pause_probe(struct platform_device *pdev)
{
	struct device_node *child;
	int registered = 0;

	for_each_available_child_of_node(pdev->dev.of_node, child) {
		struct pause_cdev *pc;
		const char *alias;
		int i, n;

		n = of_count_phandle_with_args(child, "qcom,cpus", NULL);
		if (n <= 0)
			continue;
		pc = devm_kzalloc(&pdev->dev, sizeof(*pc), GFP_KERNEL);
		if (!pc) {
			of_node_put(child);
			return -ENOMEM;
		}
		for (i = 0; i < n; i++) {
			struct device_node *cpu_np = of_parse_phandle(child, "qcom,cpus", i);
			int cpu = cpu_np ? of_cpu_node_to_id(cpu_np) : -ENODEV;

			of_node_put(cpu_np);
			if (cpu >= 0)
				cpumask_set_cpu(cpu, &pc->cpus);
		}
		if (cpumask_empty(&pc->cpus))
			continue;
		if (of_property_read_string(child, "qcom,cdev-alias", &alias))
			alias = child->name;
		strscpy(pc->name, alias, sizeof(pc->name));
		pc->np = of_node_get(child);
		INIT_WORK(&pc->work, pause_work);
		pc->cdev = thermal_of_cooling_device_register(child, pc->name, pc, &pause_ops);
		if (IS_ERR(pc->cdev)) {
			dev_err(&pdev->dev, "%s: cooling device register failed %ld\n",
				pc->name, PTR_ERR(pc->cdev));
			of_node_put(pc->np);
			continue;
		}
		list_add(&pc->node, &pause_list);
		registered++;
	}
	dev_info(&pdev->dev, "CLG8 thermal-pause (hotplug based): %d cooling devices\n", registered);
	return 0;
}

static void thermal_pause_remove(struct platform_device *pdev)
{
	struct pause_cdev *pc, *tmp;

	list_for_each_entry_safe(pc, tmp, &pause_list, node) {
		thermal_cooling_device_unregister(pc->cdev);
		cancel_work_sync(&pc->work);
		pc->target = 0;
		pause_work(&pc->work);
		of_node_put(pc->np);
		list_del(&pc->node);
	}
}

static const struct of_device_id thermal_pause_match[] = {
	{ .compatible = "qcom,thermal-pause" },
	{}
};
MODULE_DEVICE_TABLE(of, thermal_pause_match);

static struct platform_driver thermal_pause_driver = {
	.probe = thermal_pause_probe,
	.remove = thermal_pause_remove,
	.driver = {
		.name = "qti-thermal-pause-hp",
		.of_match_table = thermal_pause_match,
	},
};
module_platform_driver(thermal_pause_driver);
MODULE_DESCRIPTION("QTI thermal pause cooling devices via CPU hotplug (6.18, no WALT)");
MODULE_LICENSE("GPL");
