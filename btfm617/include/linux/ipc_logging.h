/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * pong615: zaślepka QTI ipc_logging. Modułu qcom_ipc_logging nie ma w banku 6.18,
 * więc logi IPC są wyłączone (komunikaty dev_ i pr_ zostają bez zmian).
 */
#ifndef _PONG615_IPC_LOGGING_STUB_H
#define _PONG615_IPC_LOGGING_STUB_H
#include <linux/types.h>
#include <linux/printk.h>
static char pong615_ipc_log_dummy __maybe_unused;
static inline void *ipc_log_context_create(int max_num_pages, const char *modname,
					   u32 feature_version)
{
	return &pong615_ipc_log_dummy;
}
static inline int ipc_log_context_destroy(void *ctxt) { return 0; }
#define ipc_log_string(ctxt, fmt, ...) \
	do { (void)(ctxt); no_printk(fmt, ##__VA_ARGS__); } while (0)
#endif
