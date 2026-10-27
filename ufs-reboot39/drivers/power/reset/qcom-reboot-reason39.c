// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2019 The Linux Foundation. All rights reserved.
 * Derived from native5.10 qcom-reboot-reason.c, commit673fc11462a0.
 * Local diagnostic scope: exact bootloader command and DT cell, once only.
 */
#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/slab.h>

struct reason39 {struct device *dev;struct notifier_block nb;struct nvmem_cell *cell;atomic_t once;};
static bool cell_allowed39(u32 base,u32 offset,u32 bytes,u32 bit,u32 nbits)
{return base==0x7100 && offset==0x48 && bytes==1 && bit==1 && nbits==7;}
static int reason_notify39(struct notifier_block *nb,unsigned long event,void *data)
{
 struct reason39 *r=container_of(nb,struct reason39,nb);
 const char *cmd=data;u8 value=2,got=0;size_t len=0;void *buf;int ret;
 if(event!=SYS_RESTART || !cmd || strcmp(cmd,"bootloader") || !r->cell)return NOTIFY_DONE;
 if(atomic_cmpxchg(&r->once,0,1))return NOTIFY_DONE;
 ret=nvmem_cell_write(r->cell,&value,sizeof(value));
 if(ret<0){dev_err(r->dev,"PONG618_REBOOT39 write_once=1 reason=2 errno=%d\n",ret);return NOTIFY_BAD;}
 buf=nvmem_cell_read(r->cell,&len);
 if(IS_ERR(buf)){ret=PTR_ERR(buf);dev_err(r->dev,"PONG618_REBOOT39 readback_errno=%d\n",ret);return NOTIFY_BAD;}
 if(len==1)got=*(u8 *)buf;kfree(buf);
 dev_info(r->dev,"PONG618_REBOOT39 write_once=1 reason=2 readback=%u bytes=%zu\n",got,len);
 return len==1 && got==value?NOTIFY_OK:NOTIFY_BAD;
}
static int reason_probe39(struct platform_device *pdev)
{
 struct device_node *cell,*parent;struct reason39 *r;u32 reg[2],bits[2],base;void *buf;size_t len;int ret;
 cell=of_parse_phandle(pdev->dev.of_node,"nvmem-cells",0);if(!cell)return -EINVAL;
 parent=of_get_parent(cell);
 ret=(!parent || !of_device_is_compatible(parent,"qcom,spmi-sdam") || of_property_read_u32(parent,"reg",&base) || of_property_read_u32_array(cell,"reg",reg,2) || of_property_read_u32_array(cell,"bits",bits,2) || !cell_allowed39(base,reg[0],reg[1],bits[0],bits[1]))?-EINVAL:0;
 of_node_put(parent);of_node_put(cell);if(ret)return ret;
 r=devm_kzalloc(&pdev->dev,sizeof(*r),GFP_KERNEL);if(!r)return -ENOMEM;r->dev=&pdev->dev;atomic_set(&r->once,0);
 r->cell=devm_nvmem_cell_get(r->dev,"restart_reason");if(IS_ERR(r->cell))return dev_err_probe(r->dev,PTR_ERR(r->cell),"PONG618_REBOOT39 restart cell unavailable\n");
 buf=nvmem_cell_read(r->cell,&len);if(IS_ERR(buf))return PTR_ERR(buf);kfree(buf);if(len!=1)return -EINVAL;
 r->nb.notifier_call=reason_notify39;r->nb.priority=255;ret=devm_register_reboot_notifier(r->dev,&r->nb);if(ret)return ret;
 dev_info(r->dev,"PONG618_REBOOT39 ready=1 provider=SDAM7100 offset=48 bits=1:7 cell_bytes=1 writes_at_probe=0\n");return 0;
}
static const struct of_device_id reason_match39[]={{.compatible="qcom,reboot-reason"},{}};
MODULE_DEVICE_TABLE(of,reason_match39);
static struct platform_driver reason_driver39={.probe=reason_probe39,.driver={.name="qcom-reboot-reason39",.of_match_table=reason_match39}};
module_platform_driver(reason_driver39);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Pong once-only bootloader NVMEM restart reason, native5.10 contract");
