/*
 * Goodix Touchscreen Driver
 * Copyright (C) 2020 - 2021 Goodix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the GOODiX's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */
/* Local Pong LAB profile: the optional firmware programmer is not built.
 * Existing controller firmware/configuration is retained. init/uninit own
 * no updater resources; update requests explicitly report unsupported.
 */
#include "goodix_ts_core.h"
static struct goodix_ts_core *lab_core;
int goodix_fw_update_init(struct goodix_ts_core *core_data)
{
    WRITE_ONCE(lab_core, core_data);
    ts_info("PONG381_CONTROLLER_FIRMWARE_IMMUTABLE");
    return 0;
}
int goodix_do_fw_update(struct goodix_ic_config *ic_config, int mode)
{
    return -EOPNOTSUPP;
}
void goodix_fw_update_uninit(void)
{
    WRITE_ONCE(lab_core, NULL);
}
int get_fw_version_info(struct goodix_fw_version *version)
{
    struct goodix_ts_core *core = READ_ONCE(lab_core);
    if (!core)
        return -ENODEV;
    return core->hw_ops->read_version(core, version);
}
