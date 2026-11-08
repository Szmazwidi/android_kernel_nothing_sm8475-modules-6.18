/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * CLG60: I/O-time CPU hints for the Pong (SM8475) UFS host, a reduced port of the
 * Qualcomm 5.10 / CLO 6.12 ufs-qcom QoS logic (ufs_qcom_qos, ufs_qcom_cpufreq_dwork,
 * ufs_qcom_toggle_pri_affinity) on top of the upstream driver.
 *
 *  1. Every transfer request votes DEV_PM_QOS_RESUME_LATENCY (DT child nodes qos*:
 *     mask, vote, perf) on the CPU group it was issued from, so a CPU waiting for
 *     UFS does not enter core power collapse.
 *  2. More than pong_boost_hi requests in a pong_window_ms window raise the minimum
 *     frequency of the non-perf (silver) cluster to its maximum and move the UFS IRQ
 *     to qcom,prime-mask; fewer than pong_boost_lo undo it. Completing on a CPU of the
 *     submitter's capacity also avoids the 6.18 blk-mq completion IPI
 *     (blk_mq_complete_need_ipi: cpus_equal_capacity).
 *  3. A window without requests drops all votes. 5.10 dropped them on clock gating,
 *     which the Pong 6.18 rc disables, so a timer is used instead.
 *
 * Each part has a module parameter for A/B tests. Included from ufs-qcom.c only.
 */
#ifndef UFS_QCOM_PONG_PERF_H_
#define UFS_QCOM_PONG_PERF_H_

#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/pm_qos.h>
#include <linux/workqueue.h>
#include <linux/blkdev.h>
#include <scsi/scsi_device.h>

#define PONG_PERF_MAX_GROUPS 4

static bool pong_qos = true;
module_param(pong_qos, bool, 0644);
MODULE_PARM_DESC(pong_qos, "CLG60: per-request CPU resume-latency vote from the DT qos groups");
static bool pong_boost = true;
module_param(pong_boost, bool, 0644);
MODULE_PARM_DESC(pong_boost, "CLG60: raise the silver cluster minimum frequency under heavy UFS load");
static bool pong_irq_prime = true;
module_param(pong_irq_prime, bool, 0644);
MODULE_PARM_DESC(pong_irq_prime, "CLG60: move the UFS IRQ to qcom,prime-mask under heavy UFS load");
static int pong_rq_affinity = 0;
module_param(pong_rq_affinity, int, 0444);
MODULE_PARM_DESC(pong_rq_affinity, "CLG65: block queue rq_affinity for UFS LUNs (0 complete on the IRQ CPU like 5.10, 1 kernel default, 2 force submitter CPU)");
static unsigned int pong_window_ms = 30;
module_param(pong_window_ms, uint, 0644);
static unsigned int pong_boost_hi = 64;
module_param(pong_boost_hi, uint, 0644);
static unsigned int pong_boost_lo = 32;
module_param(pong_boost_lo, uint, 0644);

struct pong_perf_group {
	cpumask_t mask;
	s32 vote;
	bool perf;
	unsigned int nreq;
	struct dev_pm_qos_request *req;
};

struct pong_perf {
	struct ufs_hba *hba;
	struct workqueue_struct *wq;
	struct work_struct vote_work;
	struct delayed_work mon_work;
	struct mutex lock;		/* serializes votes, boost and IRQ state (work items, suspend, exit) */
	int ngroups;
	struct pong_perf_group grp[PONG_PERF_MAX_GROUPS];
	s8 cpu_grp[NR_CPUS];
	unsigned long pending;		/* groups to vote, set from the issue path */
	unsigned long voted;		/* groups voted, changed under lock only */
	unsigned long mon_armed;	/* bit 0: monitor work queued */
	atomic_t reqs;			/* requests in the current window */
	/* heavy-load state, workqueue only */
	bool boosted;
	bool irq_on_prime;
	int boost_cpu;
	bool fmin_added;
	struct freq_qos_request fmin;
	cpumask_t prime_mask;
	cpumask_t silver_mask;
	/* statistics, read through pong_perf_stats */
	atomic_t n_votes, n_unvotes, n_boosts;
};

static struct pong_perf *pong_perf_inst;

static int pong_perf_stats_get(char *buf, const struct kernel_param *kp)
{
	struct pong_perf *p;
	int n;

	rcu_read_lock();	/* pong_perf_exit() clears the pointer and waits for readers before freeing */
	p = rcu_dereference(pong_perf_inst);
	if (!p)
		n = sysfs_emit(buf, "inactive\n");
	else
		n = sysfs_emit(buf, "groups=%d voted=%#lx boosted=%d irq_prime=%d votes=%d unvotes=%d boosts=%d\n",
			       p->ngroups, READ_ONCE(p->voted), READ_ONCE(p->boosted),
			       READ_ONCE(p->irq_on_prime), atomic_read(&p->n_votes),
			       atomic_read(&p->n_unvotes), atomic_read(&p->n_boosts));
	rcu_read_unlock();
	return n;
}
static const struct kernel_param_ops pong_perf_stats_ops = { .get = pong_perf_stats_get };
module_param_cb(pong_perf_stats, &pong_perf_stats_ops, NULL, 0444);

static void pong_perf_set_group(struct pong_perf *p, int g, s32 value)
{
	struct pong_perf_group *grp = &p->grp[g];
	unsigned int i;

	for (i = 0; i < grp->nreq; i++)
		dev_pm_qos_update_request(&grp->req[i], value);
}

static void pong_perf_set_irq(struct pong_perf *p, bool prime)
{
	struct ufs_hba *hba = p->hba;
	int ret;

	if (prime == p->irq_on_prime || cpumask_empty(&p->prime_mask))
		return;
	ret = irq_set_affinity_and_hint(hba->irq, prime ? &p->prime_mask : &p->silver_mask);
	if (ret) {
		/* state stays as it was, so the next window retries */
		dev_warn_ratelimited(hba->dev, "CLG60 irq affinity prime=%d: %d\n", prime, ret);
		return;
	}
	if (prime)
		irq_modify_status(hba->irq, 0, IRQ_NO_BALANCING);
	else
		irq_modify_status(hba->irq, IRQ_NO_BALANCING, 0);
	WRITE_ONCE(p->irq_on_prime, prime);
}

static void pong_perf_set_boost(struct pong_perf *p, bool on)
{
	struct cpufreq_policy *policy;

	if (on == p->boosted || p->boost_cpu < 0)
		return;
	if (!p->fmin_added) {
		/* qcom-cpufreq-hw may load after this driver: add the request on first use */
		policy = cpufreq_cpu_get(p->boost_cpu);
		if (!policy)
			return;
		if (freq_qos_add_request(&policy->constraints, &p->fmin, FREQ_QOS_MIN,
					 FREQ_QOS_MIN_DEFAULT_VALUE) >= 0)
			p->fmin_added = true;
		cpufreq_cpu_put(policy);
		if (!p->fmin_added)
			return;
	}
	if (on) {
		policy = cpufreq_cpu_get(p->boost_cpu);
		if (!policy)
			return;
		freq_qos_update_request(&p->fmin, policy->cpuinfo.max_freq);
		cpufreq_cpu_put(policy);
		atomic_inc(&p->n_boosts);
	} else {
		freq_qos_update_request(&p->fmin, FREQ_QOS_MIN_DEFAULT_VALUE);
	}
	WRITE_ONCE(p->boosted, on);
}

static void pong_perf_vote_work(struct work_struct *work)
{
	struct pong_perf *p = container_of(work, struct pong_perf, vote_work);
	unsigned long todo;
	int g;

	mutex_lock(&p->lock);
	todo = xchg(&p->pending, 0);
	for_each_set_bit(g, &todo, p->ngroups) {
		if (test_bit(g, &p->voted) || !READ_ONCE(pong_qos))
			continue;
		pong_perf_set_group(p, g, p->grp[g].vote);
		set_bit(g, &p->voted);
		atomic_inc(&p->n_votes);
	}
	mutex_unlock(&p->lock);
}

/* Called with p->lock held. */
static void pong_perf_release_all(struct pong_perf *p)
{
	int g;

	lockdep_assert_held(&p->lock);
	for (g = 0; g < p->ngroups; g++) {
		if (!test_and_clear_bit(g, &p->voted))
			continue;
		pong_perf_set_group(p, g, PM_QOS_RESUME_LATENCY_DEFAULT_VALUE);
		atomic_inc(&p->n_unvotes);
	}
	pong_perf_set_boost(p, false);
	pong_perf_set_irq(p, false);
}

static void pong_perf_mon_work(struct work_struct *work)
{
	struct pong_perf *p = container_of(to_delayed_work(work), struct pong_perf, mon_work);
	unsigned int n = atomic_xchg(&p->reqs, 0);

	mutex_lock(&p->lock);
	if (!n) {
		/*
		 * Idle window: drop everything and stop until the next request. A vote_work
		 * queued meanwhile runs after us (same lock) and votes again for the new request.
		 */
		pong_perf_release_all(p);
		mutex_unlock(&p->lock);
		clear_bit(0, &p->mon_armed);
		smp_mb__after_atomic();
		/* a request may have come in after the xchg above */
		if (atomic_read(&p->reqs) && !test_and_set_bit(0, &p->mon_armed))
			queue_delayed_work(p->wq, &p->mon_work, msecs_to_jiffies(pong_window_ms));
		return;
	}
	if (!READ_ONCE(pong_qos) && READ_ONCE(p->voted)) {
		int g;

		for (g = 0; g < p->ngroups; g++)
			if (test_and_clear_bit(g, &p->voted)) {
				pong_perf_set_group(p, g, PM_QOS_RESUME_LATENCY_DEFAULT_VALUE);
				atomic_inc(&p->n_unvotes);
			}
	}
	if (n > pong_boost_hi) {
		if (READ_ONCE(pong_boost))
			pong_perf_set_boost(p, true);
		if (READ_ONCE(pong_irq_prime))
			pong_perf_set_irq(p, true);
	} else if (n < pong_boost_lo) {
		pong_perf_set_boost(p, false);
		pong_perf_set_irq(p, false);
	}
	if (!READ_ONCE(pong_boost))
		pong_perf_set_boost(p, false);
	if (!READ_ONCE(pong_irq_prime))
		pong_perf_set_irq(p, false);
	mutex_unlock(&p->lock);
	queue_delayed_work(p->wq, &p->mon_work, msecs_to_jiffies(pong_window_ms));
}

/* Issue path: called with hba->outstanding_lock held and IRQs off. */
static void pong_perf_on_request(struct ufs_hba *hba)
{
	struct pong_perf *p = READ_ONCE(pong_perf_inst);
	int g;

	if (!p || p->hba != hba)
		return;
	atomic_inc(&p->reqs);
	g = p->cpu_grp[raw_smp_processor_id()];
	if (g >= 0 && READ_ONCE(pong_qos) && !test_bit(g, &p->voted) &&
	    !test_and_set_bit(g, &p->pending))
		queue_work(p->wq, &p->vote_work);
	if (!test_bit(0, &p->mon_armed) && !test_and_set_bit(0, &p->mon_armed))
		queue_delayed_work(p->wq, &p->mon_work, msecs_to_jiffies(pong_window_ms));
}

static void pong_perf_setup_xfer_req(struct ufs_hba *hba, int tag, bool is_scsi_cmd)
{
	if (is_scsi_cmd)
		pong_perf_on_request(hba);
}

/*
 * 6.18 blk-mq completes on the submitting CPU unless both CPUs share the LLC *and* have equal
 * capacity (blk_mq_complete_need_ipi), so on this big.LITTLE SoC almost every UFS completion
 * becomes a cross-CPU IPI (CLG58: 17k IPI1/s). 5.10 had no capacity test and completed locally.
 */
static void pong_perf_config_scsi_dev(struct scsi_device *sdev)
{
	struct request_queue *q = sdev->request_queue;

	switch (pong_rq_affinity) {
	case 0:
		blk_queue_flag_clear(QUEUE_FLAG_SAME_COMP, q);
		blk_queue_flag_clear(QUEUE_FLAG_SAME_FORCE, q);
		break;
	case 2:
		blk_queue_flag_set(QUEUE_FLAG_SAME_COMP, q);
		blk_queue_flag_set(QUEUE_FLAG_SAME_FORCE, q);
		break;
	default:
		break;
	}
}

/* Before suspend: nothing may stay voted while the system sleeps. */
static void pong_perf_quiesce(struct ufs_hba *hba)
{
	struct pong_perf *p = READ_ONCE(pong_perf_inst);

	if (!p || p->hba != hba)
		return;
	cancel_delayed_work_sync(&p->mon_work);
	flush_work(&p->vote_work);
	clear_bit(0, &p->mon_armed);
	atomic_set(&p->reqs, 0);
	mutex_lock(&p->lock);
	pong_perf_release_all(p);
	mutex_unlock(&p->lock);
}

static void pong_perf_mask_from_dt(struct device_node *np, const char *name, cpumask_t *m)
{
	u32 v = 0;
	int cpu;

	cpumask_clear(m);
	if (of_property_read_u32(np, name, &v))
		return;
	for (cpu = 0; cpu < 32 && cpu < nr_cpu_ids; cpu++)
		if (v & BIT(cpu))
			cpumask_set_cpu(cpu, m);
	cpumask_and(m, m, cpu_possible_mask);
}

static void pong_perf_free(struct pong_perf *p)
{
	int g;
	unsigned int i;

	for (g = 0; g < p->ngroups; g++) {
		for (i = 0; i < p->grp[g].nreq; i++)
			dev_pm_qos_remove_request(&p->grp[g].req[i]);
		kfree(p->grp[g].req);
	}
	if (p->fmin_added)
		freq_qos_remove_request(&p->fmin);
	if (p->wq)
		destroy_workqueue(p->wq);
	kfree(p);
}

static void pong_perf_init(struct ufs_hba *hba)
{
	struct device_node *np = hba->dev->of_node, *child;
	struct pong_perf *p;
	int cpu, err = 0;

	if (pong_perf_inst || !np)
		return;
	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return;
	p->hba = hba;
	p->boost_cpu = -1;
	memset(p->cpu_grp, -1, sizeof(p->cpu_grp));
	mutex_init(&p->lock);
	INIT_WORK(&p->vote_work, pong_perf_vote_work);
	INIT_DELAYED_WORK(&p->mon_work, pong_perf_mon_work);

	for_each_available_child_of_node(np, child) {
		struct pong_perf_group *grp;
		u32 vote;

		if (!of_node_name_prefix(child, "qos") || p->ngroups >= PONG_PERF_MAX_GROUPS)
			continue;
		grp = &p->grp[p->ngroups];
		pong_perf_mask_from_dt(child, "mask", &grp->mask);
		if (cpumask_empty(&grp->mask) || of_property_read_u32_index(child, "vote", 0, &vote))
			continue;
		grp->vote = vote;
		grp->perf = of_property_read_bool(child, "perf");
		grp->req = kcalloc(cpumask_weight(&grp->mask), sizeof(*grp->req), GFP_KERNEL);
		if (!grp->req) {
			err = -ENOMEM;
			of_node_put(child);
			break;
		}
		for_each_cpu(cpu, &grp->mask) {
			err = dev_pm_qos_add_request(get_cpu_device(cpu), &grp->req[grp->nreq],
						     DEV_PM_QOS_RESUME_LATENCY,
						     PM_QOS_RESUME_LATENCY_DEFAULT_VALUE);
			if (err < 0)
				break;
			grp->nreq++;
			if (p->cpu_grp[cpu] < 0)
				p->cpu_grp[cpu] = p->ngroups;
		}
		p->ngroups++;
		if (err < 0) {
			of_node_put(child);
			break;
		}
		err = 0;
		if (!grp->perf && p->boost_cpu < 0)
			p->boost_cpu = cpumask_first(&grp->mask);
	}
	if (err < 0 || !p->ngroups)
		goto fail;

	pong_perf_mask_from_dt(np, "qcom,prime-mask", &p->prime_mask);
	pong_perf_mask_from_dt(np, "qcom,silver-mask", &p->silver_mask);
	if (cpumask_empty(&p->silver_mask))
		cpumask_copy(&p->silver_mask, cpu_possible_mask);

	p->wq = alloc_workqueue("ufs_pong_perf", WQ_HIGHPRI | WQ_UNBOUND, 0);
	if (!p->wq)
		goto fail;
	rcu_assign_pointer(pong_perf_inst, p);
	dev_info(hba->dev, "CLG60 perf hints: %d qos groups, boost cpu %d, prime %*pbl, silver %*pbl\n",
		 p->ngroups, p->boost_cpu, cpumask_pr_args(&p->prime_mask),
		 cpumask_pr_args(&p->silver_mask));
	return;
fail:
	dev_warn(hba->dev, "CLG60 perf hints disabled (err %d, groups %d)\n", err, p->ngroups);
	pong_perf_free(p);
}

static void pong_perf_exit(struct ufs_hba *hba)
{
	struct pong_perf *p = READ_ONCE(pong_perf_inst);

	if (!p || p->hba != hba)
		return;
	WRITE_ONCE(pong_perf_inst, NULL);
	synchronize_rcu();	/* issue paths run with IRQs off */
	cancel_delayed_work_sync(&p->mon_work);
	cancel_work_sync(&p->vote_work);
	mutex_lock(&p->lock);
	pong_perf_release_all(p);
	mutex_unlock(&p->lock);
	irq_set_affinity_and_hint(hba->irq, NULL);
	pong_perf_free(p);
}

#endif
