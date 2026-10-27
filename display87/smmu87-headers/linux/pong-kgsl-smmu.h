/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PONG_KGSL_SMMU_H
#define PONG_KGSL_SMMU_H
#include <linux/iommu.h>
#include <linux/qcom-io-pgtable.h>
/* Caller holds an attached non-secure domain; returned callbacks share its lifetime. */
int pong71_smmu_get_pgtable_info(struct iommu_domain *domain,
		struct qcom_io_pgtable_info *out);
#endif
