// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 *
 */


// SPDX-License-Identifier: GPL-2.0-only
/*
 * PONG618 stage80 candidate. Derived from the API/behavior of Qualcomm's
 * panel_event_notifier.c in redducc/kernel_nothing_pong at db9180ccc252.
 * Host verified, pending 6.18 Kbuild/KMI and hardware validation.
 *
 * NOTE: Callbacks must not call unregister synchronously: unregister
 * waits for an SRCU grace period including the invoking callback.
 */
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/srcu.h>
#include <linux/soc/qcom/panel_event_notifier.h>

struct panel_event_notifier_entry {
    panel_event_notifier_handler handler;
    void *pvt_data;
    struct drm_panel *panel;
    enum panel_event_notifier_tag tag;
};

static DEFINE_MUTEX(panel_event_notifier_entries_lock);
DEFINE_SRCU(panel_event_notifier_srcu);
static struct panel_event_notifier_entry
    panel_event_notifier_entries[PANEL_EVENT_NOTIFIER_CLIENT_MAX];

static bool panel_event_notifier_tag_valid(enum panel_event_notifier_tag tag)
{
    return tag > PANEL_EVENT_NOTIFICATION_NONE &&
           tag < PANEL_EVENT_NOTIFICATION_MAX;
}

void *panel_event_notifier_register(enum panel_event_notifier_tag tag,
        enum panel_event_notifier_client client_handle,
        struct drm_panel *panel,
        panel_event_notifier_handler handler, void *pvt_data)
{
    struct panel_event_notifier_entry *entry;

    if (!panel_event_notifier_tag_valid(tag) || !panel || !handler ||
        (int)client_handle < 0 || client_handle >= PANEL_EVENT_NOTIFIER_CLIENT_MAX)
        return ERR_PTR(-EINVAL);

    mutex_lock(&panel_event_notifier_entries_lock);
    entry = &panel_event_notifier_entries[client_handle];
    if (entry->handler) {
        mutex_unlock(&panel_event_notifier_entries_lock);
        return ERR_PTR(-EEXIST);
    }

    entry->panel = panel;
    entry->pvt_data = pvt_data;
    entry->tag = tag;
    entry->handler = handler;
    mutex_unlock(&panel_event_notifier_entries_lock);
    return entry;
}
EXPORT_SYMBOL(panel_event_notifier_register);

void panel_event_notifier_unregister(void *cookie)
{
    struct panel_event_notifier_entry *entry = NULL;
    int i;

    if (!cookie || IS_ERR(cookie))
        return;

    /* Do not dereference an untrusted/stale pointer from the caller. */
    mutex_lock(&panel_event_notifier_entries_lock);
    for (i = 0; i < PANEL_EVENT_NOTIFIER_CLIENT_MAX; ++i) {
        if (cookie == &panel_event_notifier_entries[i]) {
            entry = &panel_event_notifier_entries[i];
            break;
        }
    }
    if (entry) {
        entry->handler = NULL;
        entry->pvt_data = NULL;
        entry->panel = NULL;
        entry->tag = PANEL_EVENT_NOTIFICATION_NONE;
    }
    mutex_unlock(&panel_event_notifier_entries_lock);

    if (entry)
        synchronize_srcu(&panel_event_notifier_srcu);
}
EXPORT_SYMBOL(panel_event_notifier_unregister);

void panel_event_notification_trigger(enum panel_event_notifier_tag tag,
        struct panel_event_notification *notification)
{
    panel_event_notifier_handler handler;
    void *pvt_data;
    int i, idx;

    if (!panel_event_notifier_tag_valid(tag) || !notification || !notification->panel)
        return;

    /* Unregister waits for every callback that may hold old pvt_data. */
    idx = srcu_read_lock(&panel_event_notifier_srcu);
    for (i = 0; i < PANEL_EVENT_NOTIFIER_CLIENT_MAX; ++i) {
        handler = NULL;
        pvt_data = NULL;
        mutex_lock(&panel_event_notifier_entries_lock);
        if (panel_event_notifier_entries[i].handler &&
            panel_event_notifier_entries[i].tag == tag &&
            panel_event_notifier_entries[i].panel == notification->panel) {
            handler = panel_event_notifier_entries[i].handler;
            pvt_data = panel_event_notifier_entries[i].pvt_data;
        }
        mutex_unlock(&panel_event_notifier_entries_lock);
        if (handler)
            handler(tag, notification, pvt_data);
    }
    srcu_read_unlock(&panel_event_notifier_srcu, idx);
}
EXPORT_SYMBOL(panel_event_notification_trigger);

static int __init panel_event_notifier_init(void)
{
    return 0;
}
module_init(panel_event_notifier_init);

static void __exit panel_event_notifier_exit(void)
{
    /* Module removal requires all clients to have unregistered. */
    synchronize_srcu(&panel_event_notifier_srcu);
}
module_exit(panel_event_notifier_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Pong Qualcomm panel event notifier - SRCU lifetime guard");
