/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PONG618_RPMH_VALIDATE_H
#define PONG618_RPMH_VALIDATE_H

struct rpmh_tcs_layout {
	u32 count[TCS_TYPE_NR];
	u32 offset[TCS_TYPE_NR];
	u32 mask[TCS_TYPE_NR];
	u32 total;
};

/* No side effects on failure: validation completes in a local result. */
static inline int rpmh_validate_tcs(const u32 *cfg, u32 max_tcs, u32 ncpt,
				  u32 used, struct rpmh_tcs_layout *out)
{
	struct rpmh_tcs_layout next = { 0 };
	u32 seen = 0, total = 0;
	int i;

	if (!max_tcs || !ncpt || ncpt > MAX_CMDS_PER_TCS ||
	    used > MAX_TCS_NR || used > max_tcs)
		return -EINVAL;
	for (i = 0; i < TCS_TYPE_NR; i++) {
		u32 type = cfg[2 * i], n = cfg[2 * i + 1];

		if (type >= TCS_TYPE_NR || n > MAX_TCS_PER_TYPE ||
		    (seen & (1U << type)))
			return -EINVAL;
		seen |= 1U << type;
		next.count[type] = n;
		if (!n || type == CONTROL_TCS)
			continue;
		if (used + total + n > max_tcs ||
		    used + total + n > MAX_TCS_NR || used + total + n > 32)
			return -EINVAL;
		next.offset[type] = used + total;
		next.mask[type] = ((1U << n) - 1) << next.offset[type];
		total += n;
	}
	if (!total)
		return -EINVAL;
	next.total = total;
	*out = next;
	return 0;
}

static inline int rpmh_validate_drv(u32 count, u32 id, u32 seen,
				  u32 resources, u32 irqs)
{
	/* Four 6-bit DRV counts fit below the NCPT field at bit 27. */
	if (!count || count > 4 || id >= count || id >= resources ||
	    id >= irqs || (seen & (1U << id)))
		return -EINVAL;
	return 0;
}
#endif
