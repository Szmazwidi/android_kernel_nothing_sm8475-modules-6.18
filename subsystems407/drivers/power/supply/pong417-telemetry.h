/* SPDX-License-Identifier: GPL-2.0-only */
/* LAB: read battery telemetry; never program controller firmware or set limits. */
#ifdef PONG417_HOST_TEST
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#else
#include <linux/types.h>
#include <linux/string.h>
#endif
static inline bool pong417_telemetry_allowed(const void *data, int len)
{
 unsigned int words[3];
 if (!data || len < (int)sizeof(words)) return false;
 memcpy(words, data, sizeof(words));
 if (words[0] != 32778) return false;
 switch (words[2]) {
 case 4:
 case 48:
 case 50:
 case 52:
 case 68:
  return true;
 default:return false;
 }
}
