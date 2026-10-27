/* SPDX-License-Identifier: GPL-2.0-only */
/* Local adapter: original Qualcomm SIXZONE fields, no v2 hardware mapping. */
#ifndef PONG363_SIXZONE_COMPAT_H
#define PONG363_SIXZONE_COMPAT_H
#define PONG363_SIXZONE_LEGACY_SIZE 3104U
#define PONG363_SIXZONE_FIELD_BYTES offsetof(struct drm_msm_sixzone, sat_adjust_p0)
static inline int pong363_sixzone_legacy(unsigned int version,
        unsigned long length, unsigned long expected, unsigned long long flags)
{
    return version == 0x00010007U && length == PONG363_SIXZONE_LEGACY_SIZE &&
        expected == sizeof(struct drm_msm_sixzone) &&
        !(flags & ~(unsigned long long)(SIXZONE_HUE_ENABLE |
            SIXZONE_SAT_ENABLE | SIXZONE_VAL_ENABLE));
}
static inline void pong363_sixzone_expand(struct drm_msm_sixzone *dst,
        const void *src)
{
    memset(dst, 0, sizeof(*dst));
    memcpy(dst, src, PONG363_SIXZONE_FIELD_BYTES);
}
#endif
