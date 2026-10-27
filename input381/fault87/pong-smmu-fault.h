/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PONG87_SMMU_FAULT_H
#define PONG87_SMMU_FAULT_H
#include <linux/iommu.h>
/* Handler runs under RCU in the fault thread: must not sleep or unregister.
 * Clearing with the same dev/token waits for all readers before returning.
 * One owner per domain; registration never modifies the native DMA cookie.
 */
int pong87_smmu_set_fault_handler(struct iommu_domain *domain, struct device *dev,
 iommu_fault_handler_t handler, void *token);
#endif
