/* SPDX-License-Identifier: GPL-2.0-only */
/* CLG8: Qualcomm IPC logging is not available on 6.18; BCL uses it only for optional debug logs. */
#ifndef _CLG8_IPC_LOGGING_STUB_H
#define _CLG8_IPC_LOGGING_STUB_H
static inline void *ipc_log_context_create(int max_num_pages, const char *modname, uint32_t feature_version)
{ return NULL; }
static inline int ipc_log_context_destroy(void *ctxt) { return 0; }
#define ipc_log_string(ctxt, fmt, ...) do { (void)(ctxt); } while (0)
#endif
