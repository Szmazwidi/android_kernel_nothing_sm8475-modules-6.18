// SPDX-License-Identifier: GPL-2.0-only
/* Peripheral bring-up glue for the donor Pong binding.
 * QSCRATCH definitions and VBUS sequence adapted from ACK dwc3-qcom.c:
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * Local glue uses the native snps,dwc3 child, without vendor core structures.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interconnect.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/usb/of.h>
#include <linux/usb/phy.h>
#include <linux/usb/redriver.h>
#include <linux/usb/dwc3-msm.h>

#define PONG_QSCRATCH_OFFSET 0xf8800
#define PONG_QSCRATCH_SIZE 0x400
#define PONG_GENERAL_CFG 0x08
#define PONG_PIPE_UTMI_SEL BIT(0)
#define PONG_PIPE_PHYSTATUS_SW BIT(3)
#define PONG_PIPE_UTMI_DIS BIT(8)
#define PONG_PIPE_MASK (PONG_PIPE_UTMI_SEL | PONG_PIPE_PHYSTATUS_SW | PONG_PIPE_UTMI_DIS)
#define PONG_HS_PHY_CTRL 0x10
#define PONG_SS_PHY_CTRL 0x30
#define PONG_VBUS_BITS (BIT(20) | BIT(28))
#define PONG_LANE0_POWER BIT(24)

struct pong_usb {
	struct device *dev;
	void __iomem *qscratch;
	struct regulator *gdsc;
	struct reset_control *reset;
	struct clk_bulk_data *clks;
	int num_clks;
	struct icc_path *memory_path;
	struct icc_path *config_path;
	struct usb_redriver *redriver;
	struct gpio_desc *orientation;
	bool powered;
	bool usb2_only;
	bool utmi_selected;
	u32 saved_pipe_bits;
	bool pm_off; /* CLG34 */
};

/*
 * PONG CLG34: the glue powered the controller once at probe and never released it, so the USB
 * GDSC, the XO-derived USB clocks and two untagged interconnect votes (which also apply to the
 * RPMh sleep set) stayed on in system suspend with no cable, keeping HLOS from allowing CXSD.
 * Release them in system suspend (the dwc3 child is suspended first and re-initialises its
 * core on resume) and power back on at resume.
 */
static bool pong_usb_lpm = true;
module_param(pong_usb_lpm, bool, 0644);
MODULE_PARM_DESC(pong_usb_lpm, "Pong: power the USB controller down in system suspend");

static void pong_usb_vbus(struct pong_usb *usb, bool on)
{
	u32 val = readl(usb->qscratch + PONG_HS_PHY_CTRL);
	val = on ? val | PONG_VBUS_BITS : val & ~PONG_VBUS_BITS;
	writel(val, usb->qscratch + PONG_HS_PHY_CTRL);
	readl(usb->qscratch + PONG_HS_PHY_CTRL);
	if (usb->usb2_only)
		return;
	val = readl(usb->qscratch + PONG_SS_PHY_CTRL);
	val = on ? val | PONG_LANE0_POWER : val & ~PONG_LANE0_POWER;
	writel(val, usb->qscratch + PONG_SS_PHY_CTRL);
	readl(usb->qscratch + PONG_SS_PHY_CTRL);
}

/* Sequence adapted from dwc3_qcom_select_utmi_clk(), originally introduced
 * by Manu Gautam in a4333c3a6ba9ca9cff50a3c1d1bf193dc5489e1c.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * The equivalent vendor 5.15/6.6 sequence is dwc3_msm_switch_utmi().
 * Keep clock-disable, switch and clock-enable ordering with posted-write
 * readback and ACK delays. Restore only owned bits while clocks are still on.
 */
static void pong_usb_set_pipe_bits(struct pong_usb *usb, u32 bits)
{
	u32 val = readl(usb->qscratch + PONG_GENERAL_CFG);

	writel(val | PONG_PIPE_UTMI_DIS, usb->qscratch + PONG_GENERAL_CFG);
	readl(usb->qscratch + PONG_GENERAL_CFG);
	usleep_range(100, 1000);
	val = readl(usb->qscratch + PONG_GENERAL_CFG);
	val &= ~(PONG_PIPE_UTMI_SEL | PONG_PIPE_PHYSTATUS_SW);
	val |= bits & (PONG_PIPE_UTMI_SEL | PONG_PIPE_PHYSTATUS_SW);
	writel(val, usb->qscratch + PONG_GENERAL_CFG);
	readl(usb->qscratch + PONG_GENERAL_CFG);
	usleep_range(100, 1000);
	val = readl(usb->qscratch + PONG_GENERAL_CFG);
	val = (val & ~PONG_PIPE_UTMI_DIS) | (bits & PONG_PIPE_UTMI_DIS);
	writel(val, usb->qscratch + PONG_GENERAL_CFG);
	readl(usb->qscratch + PONG_GENERAL_CFG);
}

static void pong_usb_select_utmi(struct pong_usb *usb)
{
	u32 before = readl(usb->qscratch + PONG_GENERAL_CFG);

	usb->saved_pipe_bits = before & PONG_PIPE_MASK;
	pong_usb_set_pipe_bits(usb, PONG_PIPE_UTMI_SEL | PONG_PIPE_PHYSTATUS_SW);
	usb->utmi_selected = true;
	dev_info(usb->dev, "PONG618_CLOCK33 action=select before=%#x after=%#x\n",
		 before, readl(usb->qscratch + PONG_GENERAL_CFG));
}

static void pong_usb_restore_pipe(struct pong_usb *usb)
{
	if (!usb->utmi_selected)
		return;
	pong_usb_set_pipe_bits(usb, usb->saved_pipe_bits);
	usb->utmi_selected = false;
	dev_info(usb->dev, "PONG618_CLOCK33 action=restore saved=%#x after=%#x\n",
		 usb->saved_pipe_bits, readl(usb->qscratch + PONG_GENERAL_CFG));
}

static int pong_usb_power_on(struct pong_usb *usb)
{
	int ret;
	ret = regulator_enable(usb->gdsc);
	if (ret)
		return ret;
	ret = icc_set_bw(usb->memory_path, MBps_to_icc(1000), MBps_to_icc(2500));
	if (ret)
		goto regulator_off;
	ret = icc_set_bw(usb->config_path, 0, MBps_to_icc(40));
	if (ret)
		goto memory_off;
	ret = clk_bulk_prepare_enable(usb->num_clks, usb->clks);
	if (ret)
		goto config_off;
	ret = reset_control_assert(usb->reset);
	if (ret)
		goto clocks_off;
	usleep_range(1000, 1200);
	ret = reset_control_deassert(usb->reset);
	if (ret)
		goto clocks_off;
	if (usb->usb2_only)
		pong_usb_select_utmi(usb);
	pong_usb_vbus(usb, true);
	usb->powered = true;
	return 0;
clocks_off:
	clk_bulk_disable_unprepare(usb->num_clks, usb->clks);
config_off:
	icc_set_bw(usb->config_path, 0, 0);
memory_off:
	icc_set_bw(usb->memory_path, 0, 0);
regulator_off:
	regulator_disable(usb->gdsc);
	return ret;
}

static void pong_usb_power_off(struct pong_usb *usb)
{
	if (!usb->powered)
		return;
	pong_usb_vbus(usb, false);
	pong_usb_restore_pipe(usb);
	reset_control_assert(usb->reset);
	clk_bulk_disable_unprepare(usb->num_clks, usb->clks);
	icc_set_bw(usb->config_path, 0, 0);
	icc_set_bw(usb->memory_path, 0, 0);
	regulator_disable(usb->gdsc);
	usb->powered = false;
}

static void pong_usb_redriver_put(void *data)
{
	usb_put_redriver(data);
}

static void pong_usb_device_put(void *data)
{
	put_device(data);
}

/* A high-speed diagnostic must not leave a legacy USB3 PHY phandle behind:
 * the native DWC3 core initializes every supplied legacy PHY, even in HS mode.
 * Other speed selections retain the original strict SS dependency policy.
 */
static int pong_usb_check_mode(struct device_node *child, bool *usb2_only)
{
	const char *speed;
	int ret;

	*usb2_only = false;
	if (!of_find_property(child, "maximum-speed", NULL))
		return 0;
	ret = of_property_read_string(child, "maximum-speed", &speed);
	if (ret)
		return ret;
	*usb2_only = !strcmp(speed, "high-speed");
	if (*usb2_only && of_property_count_u32_elems(child, "usb-phy") != 1)
		return -EINVAL;
	return 0;
}

static int pong_usb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *child, *np;
	struct resource *res, qscratch;
	struct usb_phy *hs, *ss;
	struct pong_usb *usb;
	struct i2c_client *redriver_client;
	const char *mode;
	u32 rate;
	int ret, orientation = 0;
	const char *pong_stage = "entry";

	child = of_get_compatible_child(dev->of_node, "snps,dwc3");
	if (!child)
		return -ENODEV;
	ret = of_property_read_string(child, "dr_mode", &mode);
	if (ret || strcmp(mode, "peripheral")) {
		of_node_put(child);
		return dev_err_probe(dev, -EOPNOTSUPP, "peripheral-only bring-up glue\n");
	}
	usb = devm_kzalloc(dev, sizeof(*usb), GFP_KERNEL);
	if (!usb) {
		of_node_put(child);
		return -ENOMEM;
	}
	usb->dev = dev;
	platform_set_drvdata(pdev, usb);
	pong_stage = "speed_contract";
	ret = pong_usb_check_mode(child, &usb->usb2_only);
	if (ret)
		goto put_child;
	pong_stage = "gdsc_supply";
	usb->gdsc = devm_regulator_get(dev, "USB3_GDSC");
	if (IS_ERR(usb->gdsc)) {
		ret = PTR_ERR(usb->gdsc);
		goto put_child;
	}
	pong_stage = "core_reset";
	usb->reset = devm_reset_control_get_exclusive(dev, "core_reset");
	if (IS_ERR(usb->reset)) {
		ret = PTR_ERR(usb->reset);
		goto put_child;
	}
	pong_stage = "clock_bulk";
	usb->num_clks = devm_clk_bulk_get_all(dev, &usb->clks);
	if (usb->num_clks < 0) {
		ret = usb->num_clks;
		goto put_child;
	}
	pong_stage = "core_rate";
	if (!of_property_read_u32(dev->of_node, "qcom,core-clk-rate", &rate)) {
		int i;
		for (i = 0; i < usb->num_clks; i++)
			if (!strcmp(usb->clks[i].id, "core_clk")) {
				ret = clk_set_rate(usb->clks[i].clk, rate);
				if (ret)
					goto put_child;
			}
	}
	pong_stage = "memory_icc";
	usb->memory_path = devm_of_icc_get(dev, "usb-ddr");
	if (IS_ERR(usb->memory_path)) {
		ret = PTR_ERR(usb->memory_path);
		goto put_child;
	}
	pong_stage = "config_icc";
	usb->config_path = devm_of_icc_get(dev, "ddr-usb");
	if (IS_ERR(usb->config_path)) {
		ret = PTR_ERR(usb->config_path);
		goto put_child;
	}
	pong_stage = "qscratch";
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "core_base");
	if (!res || resource_size(res) < PONG_QSCRATCH_OFFSET + PONG_QSCRATCH_SIZE) {
		ret = -EINVAL;
		goto put_child;
	}
	qscratch = (struct resource)DEFINE_RES_MEM(res->start + PONG_QSCRATCH_OFFSET, PONG_QSCRATCH_SIZE);
	usb->qscratch = devm_ioremap_resource(dev, &qscratch);
	if (IS_ERR(usb->qscratch)) {
		ret = PTR_ERR(usb->qscratch);
		goto put_child;
	}
	pong_stage = "hs_phy";
	np = of_parse_phandle(child, "usb-phy", 0);
	if (!np) {
		ret = -EINVAL;
		goto put_child;
	}
	hs = devm_usb_get_phy_by_node(dev, np, NULL);
	of_node_put(np);
	if (IS_ERR(hs)) {
		ret = PTR_ERR(hs);
		goto put_child;
	}
	hs->flags &= ~PHY_HOST_MODE;
	if (!usb->usb2_only) {
		pong_stage = "ss_phy";
		np = of_parse_phandle(child, "usb-phy", 1);
		if (!np) {
			ret = -EINVAL;
			goto put_child;
		}
		ss = devm_usb_get_phy_by_node(dev, np, NULL);
		of_node_put(np);
		if (IS_ERR(ss)) {
			ret = PTR_ERR(ss);
			goto put_child;
		}
		pong_stage = "orientation_gpio";
		usb->orientation = devm_gpiod_get_optional(dev, NULL, GPIOD_IN);
		if (IS_ERR(usb->orientation)) {
			ret = PTR_ERR(usb->orientation);
			goto put_child;
		}
		orientation = usb->orientation ? gpiod_get_value_cansleep(usb->orientation) : 0;
		if (orientation < 0) {
			ret = orientation;
			goto put_child;
		}
		ss->flags &= ~(PHY_HOST_MODE | PHY_LANE_A | PHY_LANE_B);
		ss->flags |= orientation ? PHY_LANE_B : PHY_LANE_A;
		pong_stage = "redriver";
		np = of_parse_phandle(dev->of_node, "ssusb_redriver", 0);
		if (!np) {
			ret = -EINVAL;
			goto put_child;
		}
		redriver_client = of_find_i2c_device_by_node(np);
		of_node_put(np);
		if (!redriver_client) {
			ret = -EPROBE_DEFER;
			goto put_child;
		}
		ret = devm_add_action_or_reset(dev, pong_usb_device_put, &redriver_client->dev);
		if (ret)
			goto put_child;
		if (!device_link_add(dev, &redriver_client->dev, DL_FLAG_AUTOREMOVE_CONSUMER)) {
			ret = -ENOMEM;
			goto put_child;
		}
		usb->redriver = usb_get_redriver_by_phandle(dev->of_node, "ssusb_redriver", 0);
		if (IS_ERR_OR_NULL(usb->redriver)) {
			ret = usb->redriver ? PTR_ERR(usb->redriver) : -EPROBE_DEFER;
			goto put_child;
		}
		ret = devm_add_action_or_reset(dev, pong_usb_redriver_put, usb->redriver);
		if (ret)
			goto put_child;
	}
	pong_stage = "power_on";
	ret = pong_usb_power_on(usb);
	if (ret)
		goto put_child;
	if (!usb->usb2_only) {
		pong_stage = "redriver_connect";
		ret = usb_redriver_connect_checked(usb->redriver, orientation ? ORIENTATION_CC2 : ORIENTATION_CC1);
		if (ret) { pong_usb_power_off(usb); goto put_child; }
	}
	pong_stage = "populate_core";
	ret = of_platform_populate(dev->of_node, NULL, NULL, dev);
	if (ret) {
		of_platform_depopulate(dev);
		if (!usb->usb2_only)
			usb_redriver_disconnect_checked(usb->redriver);
		pong_usb_power_off(usb);
	}
put_child:
	dev_info(dev, "PONG618_PROBE33 mode=%s stage=%s ret=%d\n", usb->usb2_only ? "high-speed" : "superspeed", pong_stage, ret);
	if (ret)
		dev_err_probe(dev, ret, "PONG618_PROBE33 %s\n", pong_stage);
	of_node_put(child);
	return ret;
}

static void pong_usb_remove(struct platform_device *pdev)
{
	struct pong_usb *usb = platform_get_drvdata(pdev);
	of_platform_depopulate(&pdev->dev);
	if (!usb->usb2_only)
		usb_redriver_disconnect_checked(usb->redriver);
	pong_usb_power_off(usb);
}

static int pong_usb_pm_suspend(struct device *dev)
{
	struct pong_usb *usb = dev_get_drvdata(dev);

	if (!usb || !pong_usb_lpm || !usb->powered)
		return 0;
	pong_usb_power_off(usb);
	usb->pm_off = true;
	return 0;
}

static int pong_usb_pm_resume(struct device *dev)
{
	struct pong_usb *usb = dev_get_drvdata(dev);
	int ret;

	if (!usb || !usb->pm_off)
		return 0;
	usb->pm_off = false;
	ret = pong_usb_power_on(usb);
	if (ret)
		dev_err(dev, "PONG618_USB34 resume power on failed: %d\n", ret);
	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(pong_usb_pm_ops, pong_usb_pm_suspend, pong_usb_pm_resume);

static const struct of_device_id pong_usb_match[] = {
	{ .compatible = "qcom,dwc-usb3-msm" }, { }
};
MODULE_DEVICE_TABLE(of, pong_usb_match);
static struct platform_driver pong_usb_driver = {
	.probe = pong_usb_probe, .remove = pong_usb_remove,
	.driver = { .name = "pong-dwc3-peripheral", .of_match_table = pong_usb_match,
		    .pm = pm_sleep_ptr(&pong_usb_pm_ops) },
};
module_platform_driver(pong_usb_driver);
MODULE_DESCRIPTION("Pong diagnostic peripheral glue using native DWC3 child");
MODULE_LICENSE("GPL");
