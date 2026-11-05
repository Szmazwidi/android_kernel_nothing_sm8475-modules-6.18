/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_PONG_SYSTEM_HEAP_H
#define _LINUX_PONG_SYSTEM_HEAP_H

#include <linux/types.h>

struct dma_buf;

bool pong_system_heap_owns_buffer(const struct dma_buf *dmabuf);

#endif
