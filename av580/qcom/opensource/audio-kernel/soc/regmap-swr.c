// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/unaligned.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/init.h>
#include <soc/soundwire.h>

/* The legacy Qualcomm SoundWire bus uses native 16-bit registers and 8-bit values. */
#define SWR_REG_BYTES (sizeof(u16))
#define SWR_ENTRY_BYTES (SWR_REG_BYTES + sizeof(u8))

static bool regmap_swr_valid_config(const struct regmap_config *config)
{
	return config->reg_bits == 16 && config->val_bits == 8 &&
	       !config->pad_bits;
}

static int regmap_swr_gather_write(void *context,
				const void *reg, size_t reg_size,
				const void *val, size_t val_len)
{
	struct device *dev = context;
	struct swr_device *swr = to_swr_device(dev);
	size_t i;
	int ret = 0;
	u16 reg_addr;

	if (swr == NULL) {
		dev_err(dev, "%s: swr device is NULL\n", __func__);
		return -EINVAL;
	}
	if (reg_size != SWR_REG_BYTES) {
		dev_err(dev, "%s: reg size %zd bytes not supported\n",
			__func__, reg_size);
		return -EINVAL;
	}
	reg_addr = get_unaligned((const u16 *)reg);
	for (i = 0; i < val_len; i++) {
		u8 value = ((const u8 *)val)[i];

		ret = swr_write(swr, swr->dev_num, reg_addr + i, &value);
		if (ret < 0) {
			dev_err(dev, "%s: write reg 0x%zx failed, err %d\n",
				__func__, reg_addr + i, ret);
			break;
		}
	}
	return ret;
}

static int regmap_swr_raw_multi_reg_write(void *context, const void *data,
					  size_t count)
{
	struct device *dev = context;
	struct swr_device *swr = to_swr_device(dev);
	size_t num_regs;
	size_t i;
	int ret = 0;
	u16 *reg;
	u8 *val;
	const u8 *buf = data;

	if (swr == NULL) {
		dev_err(dev, "%s: swr device is NULL\n", __func__);
		return -EINVAL;
	}

	if (!count || count % SWR_ENTRY_BYTES)
		return -EINVAL;
	num_regs = count / SWR_ENTRY_BYTES;

	reg = kcalloc(num_regs, sizeof(u16), GFP_KERNEL);
	if (!reg)
		return -ENOMEM;

	val = kcalloc(num_regs, sizeof(u8), GFP_KERNEL);
	if (!val) {
		ret = -ENOMEM;
		goto mem_fail;
	}

	for (i = 0; i < num_regs; i++, buf += SWR_ENTRY_BYTES) {
		reg[i] = get_unaligned((const u16 *)buf);
		val[i] = buf[SWR_REG_BYTES];
	}
	ret = swr_bulk_write(swr, swr->dev_num, reg, val, num_regs);
	if (ret)
		dev_err(dev, "%s: multi reg write failed\n", __func__);

	kfree(val);
mem_fail:
	kfree(reg);
	return ret;
}

static int regmap_swr_write(void *context, const void *data, size_t count)
{
	const u8 *buf = data;

	if (count < SWR_ENTRY_BYTES)
		return -EINVAL;
	if (count > SWR_ENTRY_BYTES)
		return regmap_swr_raw_multi_reg_write(context, data, count);

	return regmap_swr_gather_write(context, buf, SWR_REG_BYTES,
				      buf + SWR_REG_BYTES, sizeof(u8));
}

static int regmap_swr_read(void *context,
			const void *reg, size_t reg_size,
			void *val, size_t val_size)
{
	struct device *dev = context;
	struct swr_device *swr = to_swr_device(dev);
	int ret = 0;
	u16 reg_addr;

	if (swr == NULL) {
		dev_err(dev, "%s: swr is NULL\n", __func__);
		return -EINVAL;
	}
	if (reg_size != SWR_REG_BYTES) {
		dev_err(dev, "%s: register size %zd bytes not supported\n",
			__func__, reg_size);
		return -EINVAL;
	}
	reg_addr = get_unaligned((const u16 *)reg);
	ret = swr_read(swr, swr->dev_num, reg_addr, val, val_size);
	if (ret < 0)
		dev_err(dev, "%s: codec reg 0x%x read failed %d\n",
			__func__, reg_addr, ret);
	return ret;
}

static struct regmap_bus regmap_swr = {
	.write = regmap_swr_write,
	.gather_write = regmap_swr_gather_write,
	.read = regmap_swr_read,
	.reg_format_endian_default = REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default = REGMAP_ENDIAN_NATIVE,
};

struct regmap *__regmap_init_swr(struct swr_device *swr,
				 const struct regmap_config *config,
				 struct lock_class_key *lock_key,
				 const char *lock_name);

struct regmap *__regmap_init_swr(struct swr_device *swr,
				 const struct regmap_config *config,
				 struct lock_class_key *lock_key,
				 const char *lock_name)
{
	if (!regmap_swr_valid_config(config))
		return ERR_PTR(-EINVAL);

	return __regmap_init(&swr->dev, &regmap_swr, &swr->dev, config,
			   lock_key, lock_name);
}
EXPORT_SYMBOL(__regmap_init_swr);

struct regmap *__devm_regmap_init_swr(struct swr_device *swr,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key,
				      const char *lock_name);

struct regmap *__devm_regmap_init_swr(struct swr_device *swr,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key,
				      const char *lock_name)
{
	if (!regmap_swr_valid_config(config))
		return ERR_PTR(-EINVAL);

	return __devm_regmap_init(&swr->dev, &regmap_swr, &swr->dev, config,
				lock_key, lock_name);
}
EXPORT_SYMBOL(__devm_regmap_init_swr);

MODULE_LICENSE("GPL v2");
