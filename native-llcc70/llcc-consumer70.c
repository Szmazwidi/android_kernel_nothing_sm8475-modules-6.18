// SPDX-License-Identifier: GPL-2.0-only
/* Local diagnostic consumer; obtains and releases descriptors, no GPU activation. */
#include <linux/module.h>
#include <linux/err.h>
#include <linux/soc/qcom/llcc-qcom.h>
static int result=-EPROBE_DEFER;
module_param(result,int,0444);
static int __init consumer70_init(void){struct llcc_slice_desc *g=llcc_slice_getd(LLCC_GPU),*h=llcc_slice_getd(LLCC_GPUHTW);if(IS_ERR(g)||IS_ERR(h)){result=IS_ERR(g)?PTR_ERR(g):PTR_ERR(h);if(!IS_ERR(g))llcc_slice_putd(g);if(!IS_ERR(h))llcc_slice_putd(h);pr_info("PONG70_LLCC_DESCRIPTOR_RESULT=%d\n",result);return 0;}result=(llcc_get_slice_id(g)==12&&llcc_get_slice_size(g)==2048&&llcc_get_slice_id(h)==11&&llcc_get_slice_size(h)==512)?0:-EINVAL;pr_info("PONG70_LLCC_DESCRIPTORS gpu=%u:%zu gpuhtw=%u:%zu result=%d\n",g->slice_id,g->slice_size,h->slice_id,h->slice_size,result);llcc_slice_putd(g);llcc_slice_putd(h);return 0;}
static void __exit consumer70_exit(void){}
module_init(consumer70_init);module_exit(consumer70_exit);MODULE_LICENSE("GPL");MODULE_DESCRIPTION("Pong70 readonly LLCC descriptor consumer");
