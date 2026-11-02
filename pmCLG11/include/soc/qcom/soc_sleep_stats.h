/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#ifndef __SOC_SLEEP_STATS_H__
#define __SOC_SLEEP_STATS_H__

struct ddr_freq_residency {
	uint32_t freq;
	uint64_t residency;
};

struct ddr_stats_ss_vote_info {
	u32 ab; /* vote_x */
	u32 ib; /* vote_y */
};

#if IS_ENABLED(CONFIG_QCOM_SOC_SLEEP_STATS)
int ddr_stats_freq_sync_send_msg(void);
int ddr_stats_get_freq_count(void);
int ddr_stats_get_residency(int freq_count, struct ddr_freq_residency *data);
#else
/* CLG11: vendor soc_sleep_stats is replaced by upstream qcom_stats; DDR residency stats unavailable */
static inline int ddr_stats_freq_sync_send_msg(void) { return -ENODEV; }
static inline int ddr_stats_get_freq_count(void) { return -ENODEV; }
static inline int ddr_stats_get_residency(int freq_count, struct ddr_freq_residency *data) { return -ENODEV; }
#endif
#if IS_ENABLED(CONFIG_QCOM_SOC_SLEEP_STATS)

int ddr_stats_get_ss_count(void);
int ddr_stats_get_ss_vote_info(int ss_count,
			       struct ddr_stats_ss_vote_info *vote_info);
#else

static inline int ddr_stats_get_ss_count(void)
{return -ENODEV; }
static inline int ddr_stats_get_ss_vote_info(int ss_count,
			       struct ddr_stats_ss_vote_info *vote_info)
{ return -ENODEV; }

#endif
#endif /*__SOC_SLEEP_STATS_H__ */
