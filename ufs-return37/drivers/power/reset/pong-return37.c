// SPDX-License-Identifier: GPL-2.0-only
/* Bootloader IMEM contract: Copyright (c) 2019 The Linux Foundation.
 * Narrow local diagnostic adapter, derived from the exact working5.10
 * msm-poweroff.c contract. No PON, dload, partition or normal-reboot writes.
 */
#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/reboot.h>

#define PONG37_IMEM_ADDRESS 0x146aa65cULL
#define PONG37_BOOTLOADER_MAGIC 0x77665500U
static void __iomem *reason37;
static atomic_t wrote37 = ATOMIC_INIT(0);
static bool registered37;

static bool resource_allowed37(unsigned long long start, unsigned long long bytes)
{
 return start == PONG37_IMEM_ADDRESS && bytes == sizeof(u32);
}
static int return37_notify(struct notifier_block *nb, unsigned long event, void *data)
{
 const char *cmd = data;
 u32 got;
 if (event != SYS_RESTART || !cmd || strcmp(cmd, "bootloader") || !reason37)
  return NOTIFY_DONE;
 if (atomic_cmpxchg(&wrote37, 0, 1)) return NOTIFY_DONE;
 writel_relaxed(PONG37_BOOTLOADER_MAGIC, reason37);
 wmb();
 got = readl_relaxed(reason37);
 pr_info("PONG618_RETURN37 action=bootloader write_once=1 value=%#x readback=%#x\n",
  PONG37_BOOTLOADER_MAGIC, got);
 return got == PONG37_BOOTLOADER_MAGIC ? NOTIFY_OK : NOTIFY_BAD;
}
static struct notifier_block return37_nb = { .notifier_call = return37_notify, .priority = 255 };
static int __init return37_init(void)
{
 struct device_node *node;
 struct resource r;
 int ret;
 node = of_find_compatible_node(NULL, NULL, "qcom,msm-imem-restart_reason");
 if (!node) { pr_info("PONG618_RETURN37 unavailable=no_DT_node\n"); return 0; }
 ret = of_address_to_resource(node, 0, &r);
 if (ret || !of_device_is_available(node) || !resource_allowed37(r.start, resource_size(&r))) {
  of_node_put(node); return ret ? ret : -EINVAL;
 }
 reason37 = of_iomap(node, 0);
 of_node_put(node);
 if (!reason37) return -ENOMEM;
 ret = register_reboot_notifier(&return37_nb);
 if (ret) { iounmap(reason37); reason37 = NULL; return ret; }
 registered37 = true;
 pr_info("PONG618_RETURN37 ready=1 address=%#llx bytes=4 initial=%#x\n",
  PONG37_IMEM_ADDRESS, readl_relaxed(reason37));
 return 0;
}
static void __exit return37_exit(void)
{
 if (registered37) unregister_reboot_notifier(&return37_nb);
 if (reason37) iounmap(reason37);
}
module_init(return37_init);
module_exit(return37_exit);
MODULE_DESCRIPTION("Pong RAM diagnostic once-only IMEM bootloader return");
MODULE_LICENSE("GPL v2");
