/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __LINUX_USB_DWC3_MSM_H
#define __LINUX_USB_DWC3_MSM_H

/* used for struct usb_phy flags */
#define PHY_HOST_MODE			BIT(0)
#define DEVICE_IN_SS_MODE		BIT(1)
#define PHY_LANE_A			BIT(2)
#define PHY_LANE_B			BIT(3)
#define PHY_HSFS_MODE			BIT(4)
#define PHY_LS_MODE			BIT(5)
#define EUD_SPOOF_DISCONNECT		BIT(6)
#define EUD_SPOOF_CONNECT		BIT(7)
#define PHY_SUS_OVERRIDE		BIT(8)
#define PHY_DP_MODE			BIT(9)
#define PHY_USB_DP_CONCURRENT_MODE	BIT(10)
#define PHY_SS_PHY_DYNAMIC_POWERDOWN	BIT(11)


#endif /* __LINUX_USB_DWC3_MSM_H */
