/* SPDX-License-Identifier: GPL-2.0 */
/* pong615: vendorowe rozszerzenie API slimbus (Qualcomm, z redducc 6.6 db9180cc). */
#ifndef _PONG615_SLIMBUS_EXT_H
#define _PONG615_SLIMBUS_EXT_H
#include <linux/slimbus.h>
int slim_stream_unprepare_disconnect_port(struct slim_stream_runtime *stream,
		bool disconnect_ports, bool is_session_completed);
#endif
