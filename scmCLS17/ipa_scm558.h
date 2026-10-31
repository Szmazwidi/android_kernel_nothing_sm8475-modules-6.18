/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PONG_IPA_SCM558_H
#define PONG_IPA_SCM558_H
#include <linux/types.h>
#define QCOM_SCM_SVC_UTIL 0x03
#define QCOM_SCM_UTIL_GET_SEC_DUMP_STATE 0x10
#define QCOM_SCM_MP_MEM_PROTECT_REGION_ID 0x10
int qcom_scm_mem_protect_region_id(phys_addr_t paddr, size_t size);
int qcom_scm_get_sec_dump_state(u32 *dump_state);
#endif
