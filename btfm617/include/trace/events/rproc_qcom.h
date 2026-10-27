/* SPDX-License-Identifier: GPL-2.0-only */
/* pong615: zaślepka tracepointu rproc_qcom_event (bez zależności od ABI vendorowego tracepointu). */
#ifndef _PONG615_RPROC_QCOM_TRACE_STUB_H
#define _PONG615_RPROC_QCOM_TRACE_STUB_H
static inline void trace_rproc_qcom_event(const char *name, const char *event,
					  const char *subevent) { }
#endif
