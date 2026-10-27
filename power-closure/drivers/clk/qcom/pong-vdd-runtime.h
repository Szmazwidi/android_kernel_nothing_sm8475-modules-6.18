/* SPDX-License-Identifier: GPL-2.0-only */
/* Called under the CCF prepare lock: aggregate votes, raise before rate changes,
 * handle ABORT explicitly and release only after unprepare/rate completion.
 * No changes to native public or shared private clock object layouts.
 */
static int pong_vdd_level(struct pong_vdd_clock *c, unsigned long rate)
{
 unsigned int i;
 for (i = 1; i < VDD_NUM; i++)
  if (c->rate_max[i] && DIV_ROUND_CLOSEST(rate, 1000) <=
      DIV_ROUND_CLOSEST(c->rate_max[i], 1000)) return i;
 return -ERANGE;
}
static int pong_vdd_set_rail(struct pong_vdd_rail *r, unsigned int level)
{
 unsigned int old = r->level;
 int ret, rollback;
 if (level == old) return 0;
 ret = regulator_set_voltage(r->reg, vdd_corner[level], INT_MAX);
 if (ret) return ret;
 if (!old && level) ret = regulator_enable(r->reg);
 else if (old && !level) ret = regulator_disable(r->reg);
 if (ret) {
  rollback = regulator_set_voltage(r->reg, vdd_corner[old], INT_MAX);
  if (rollback) pr_err("pong GCC voltage rollback failed: %d\n", rollback);
  return ret;
 }
 r->level = level;
 return 0;
}
static int pong_vdd_vote_locked(struct pong_vdd_clock *changed, unsigned int level)
{
 unsigned int target[2] = {pong_vdd_boot_level, pong_vdd_boot_level}, old[2], i, rail;
 int ret, rollback;
 for (i = 0; i < ARRAY_SIZE(pong_vdd_clocks); i++) {
  struct pong_vdd_clock *c = &pong_vdd_clocks[i];
  unsigned int vote = c == changed ? level : c->active;
  for (rail = 0; rail < 2; rail++)
   if (c->rails & BIT(rail)) target[rail] = max(target[rail], vote);
 }
 for (rail = 0; rail < 2; rail++) old[rail] = pong_vdd_rails[rail].level;
 for (rail = 0; rail < 2; rail++) {
  ret = pong_vdd_set_rail(&pong_vdd_rails[rail], target[rail]);
  if (ret) {
   while (rail--) {
    rollback = pong_vdd_set_rail(&pong_vdd_rails[rail], old[rail]);
    if (rollback) pr_err("pong GCC rail rollback failed: %d\n", rollback);
   }
   return ret;
  }
 }
 if (changed) changed->active = level;
 return 0;
}
static int pong_vdd_vote(struct pong_vdd_clock *changed, unsigned int level)
{
 int ret;
 mutex_lock(&pong_vdd_lock);
 ret = pong_vdd_vote_locked(changed, level);
 mutex_unlock(&pong_vdd_lock);
 return ret;
}
static int pong_vdd_set_boot(unsigned int level)
{
 unsigned int old;
 int ret;
 mutex_lock(&pong_vdd_lock);
 old = pong_vdd_boot_level;
 pong_vdd_boot_level = level;
 ret = pong_vdd_vote_locked(NULL, 0);
 if (ret) pong_vdd_boot_level = old;
 mutex_unlock(&pong_vdd_lock);
 return ret;
}
static struct pong_vdd_clock *pong_vdd_find(struct clk_hw *hw)
{
 unsigned int i;
 for (i = 0; i < ARRAY_SIZE(pong_vdd_clocks); i++)
  if (pong_vdd_clocks[i].hw == hw) return &pong_vdd_clocks[i];
 return NULL;
}
static int pong_vdd_prepare(struct clk_hw *hw)
{
 struct pong_vdd_clock *c = pong_vdd_find(hw);
 int level, ret;
 if (!c) return -EINVAL;
 level = pong_vdd_level(c, clk_hw_get_rate(hw));
 if (level < 0) return level;
 ret = pong_vdd_vote(c, level);
 if (ret) return ret;
 if (c->original_ops->prepare) {
  ret = c->original_ops->prepare(hw);
  if (ret) {
   if (pong_vdd_vote(c, 0)) pr_err("pong GCC prepare rollback retained voltage vote\n");
   return ret;
  }
 }
 c->prepared = true;
 return 0;
}
static void pong_vdd_unprepare(struct clk_hw *hw)
{
 struct pong_vdd_clock *c = pong_vdd_find(hw);
 int ret;
 if (!c) return;
 if (c->original_ops->unprepare) c->original_ops->unprepare(hw);
 c->prepared = c->pending = false;
 ret = pong_vdd_vote(c, 0);
 if (ret) pr_err("pong GCC unprepare retained voltage vote: %d\n", ret);
}
static int pong_vdd_notify(struct notifier_block *nb, unsigned long event, void *data)
{
 struct pong_vdd_clock *c = container_of(nb, struct pong_vdd_clock, notifier);
 struct clk_notifier_data *rate = data;
 int level, ret = 0;
 if (event == PRE_RATE_CHANGE) {
  level = pong_vdd_level(c, rate->new_rate);
  if (level < 0) return notifier_from_errno(level);
  if (c->pending) return notifier_from_errno(-EBUSY);
  c->saved = c->active;
  c->pending = true;
  if (c->prepared && level > c->active) ret = pong_vdd_vote(c, level);
  if (ret) c->pending = false;
 } else if (event == ABORT_RATE_CHANGE && c->pending) {
  ret = pong_vdd_vote(c, c->saved);
  c->pending = false;
 } else if (event == POST_RATE_CHANGE) {
  level = pong_vdd_level(c, rate->new_rate);
  if (level < 0) ret = level;
  else if (c->prepared) ret = pong_vdd_vote(c, level);
  c->pending = false;
 }
 return notifier_from_errno(ret);
}
static void pong_vdd_restore(void *unused)
{
 unsigned int i;
 if (pong_vdd_set_boot(0)) pr_err("pong GCC cleanup retained boot voltage vote\n");
 for (i = 0; i < ARRAY_SIZE(pong_vdd_clocks); i++) {
  struct pong_vdd_clock *c = &pong_vdd_clocks[i];
  if (c->original_init) c->hw->init = c->original_init;
  if (pong_vdd_vote(c, 0)) pr_err("pong GCC cleanup retained voltage vote\n");
  c->prepared = c->pending = false;
 }
}
static void pong_vdd_unregister(void *unused)
{
 unsigned int i;
 for (i = 0; i < ARRAY_SIZE(pong_vdd_clocks); i++) {
  struct pong_vdd_clock *c = &pong_vdd_clocks[i];
  if (c->registered) clk_notifier_unregister(c->clk, &c->notifier);
  if (c->clk) clk_put(c->clk);
  c->registered = false;
  c->clk = NULL;
 }
}
static int pong_vdd_init(struct device *dev)
{
 static const char * const supplies[] = { "vdd_cx", "vdd_mxa" };
 unsigned int i;
 for (i = 0; i < 2; i++) {
  pong_vdd_rails[i].reg = devm_regulator_get(dev, supplies[i]);
  if (IS_ERR(pong_vdd_rails[i].reg)) return dev_err_probe(dev, PTR_ERR(pong_vdd_rails[i].reg), "GCC voltage supplier\n");
 }
 for (i = 0; i < ARRAY_SIZE(pong_vdd_clocks); i++) {
  struct pong_vdd_clock *c = &pong_vdd_clocks[i];
  c->original_init = c->hw->init;
  c->original_ops = c->hw->init->ops;
  c->init = *c->original_init;
  c->ops = *c->original_ops;
  c->ops.prepare = pong_vdd_prepare;
  c->ops.unprepare = pong_vdd_unprepare;
  c->init.ops = &c->ops;
  c->hw->init = &c->init;
  c->notifier.notifier_call = pong_vdd_notify;
 }
 {
  int ret = devm_add_action_or_reset(dev, pong_vdd_restore, NULL);
  if (ret) return ret;
 }
 return pong_vdd_set_boot(VDD_HIGH_L1);
}
static void pong_vdd_sync_state(struct device *dev)
{
 if (pong_vdd_set_boot(0)) dev_warn(dev, "GCC boot voltage vote retained\n");
}
static int pong_vdd_register(struct device *dev)
{
 unsigned int i;
 int ret = devm_add_action_or_reset(dev, pong_vdd_unregister, NULL);
 if (ret) return ret;
 for (i = 0; i < ARRAY_SIZE(pong_vdd_clocks); i++) {
  struct pong_vdd_clock *c = &pong_vdd_clocks[i];
  c->clk = clk_hw_get_clk(c->hw, NULL);
  if (IS_ERR(c->clk)) { ret = PTR_ERR(c->clk); c->clk = NULL; return ret; }
  ret = clk_notifier_register(c->clk, &c->notifier);
  if (ret) return ret;
  c->registered = true;
 }
 return 0;
}
