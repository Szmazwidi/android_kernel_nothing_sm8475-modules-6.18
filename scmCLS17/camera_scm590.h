/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PONG_CAMERA_SCM590_H
#define PONG_CAMERA_SCM590_H
#include <linux/types.h>
#define QCOM_SCM_SVC_SMMU_PROGRAM 0x15
#define QCOM_SCM_SMMU_SECURE_LUT 0x03
#define QCOM_SCM_SVC_CAMERA 0x18
#define QCOM_SCM_CAMERA_PROTECT_PHY_LANES 0x07
int qcom_scm_smmu_notify_secure_lut(u64 dev_id, bool secure);
int qcom_scm_camera_protect_phy_lanes(bool protect, u64 regmask);
#endif
