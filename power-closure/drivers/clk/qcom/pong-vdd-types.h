/* SPDX-License-Identifier: GPL-2.0-only */
/* Local native-CCF bridge; voltage tables derive from pinned GCC CAPE. */
#include <linux/notifier.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include "vdd-level.h"
struct pong_vdd_clock {
 struct clk_hw *hw;
 unsigned int rails;
 unsigned long rate_max[VDD_NUM];
 unsigned int active, saved;
 bool prepared, pending, registered;
 const struct clk_init_data *original_init;
 const struct clk_ops *original_ops;
 struct clk_init_data init;
 struct clk_ops ops;
 struct notifier_block notifier;
 struct clk *clk;
};
struct pong_vdd_rail {
 struct regulator *reg;
 unsigned int level;
};
static struct pong_vdd_rail pong_vdd_rails[2];
static DEFINE_MUTEX(pong_vdd_lock);
static unsigned int pong_vdd_boot_level;
