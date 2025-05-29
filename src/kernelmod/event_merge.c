// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/string.h>

#include "event_merge.h"
#include "event.h"
#include "vfs_change_consts.h"

static int (*vfs_changed_entry)(struct vfs_event *event);
static LIST_HEAD(vfs_events);
static DEFINE_SPINLOCK(sl_vfs_events);
static int events_number;
static int quit;

// #define mpr_log(fmt, ...) pr_info("vfs_monitor: " fmt, ##__VA_ARGS__)
#define mpr_log(fmt, ...) ;

typedef int (*merge_action_fn_t)(struct list_head* entry, struct vfs_event *cur);

/*
 * new ren_to will be paired, or update to new event
 * ren_fr will send after paired
 * ren_fr which already be moved, will unpaired with ren_to
 * unpaired ren_to will not join merge
 *
 * the event that will be removed, will serve as a match
 */

#define MERGE_OK    0
#define MERGE_FAIL  1

#define REMOVE_ENTRY(p, e) {\
    list_del(p);\
    vfs_event_free(e);\
    --events_number;\
}

#define cmp_event_path(e1, e2) e1->dev != e2->dev ||  strcmp(e1->path, e2->path)

/*
 * merge rules
 *
 * del(X) + new(X)                                 => remove del(X)
 * ren_fr(X) + ren_to(Y) + new(X)                  => update ren_to(Y) to new(Y), remove ren_fr(X)
 *
 * merge success, remove cur, else add to list
 */
static int merge_new_file(struct list_head* p, struct vfs_event *cur)
{
    struct vfs_event *e = list_entry(p, struct vfs_event, list);
    if (ACT_DEL_FILE == e->action) {
        if (cmp_event_path(e, cur))
            return MERGE_FAIL;
        REMOVE_ENTRY(p, e);
        return MERGE_OK;
    } else if (ACT_RENAME_FROM_FILE == e->action) {
        if (cmp_event_path(e, cur))
            return MERGE_FAIL;
        /*
         * if ren_fr alread paired, then update ren_to
         * else, it means ren_to still not insert, it will be update when it enter do_event_merge function
         */
        if (e->pair) {
            ((struct vfs_event *)e->pair)->action = ACT_NEW_FILE;
            ((struct vfs_event *)e->pair)->cookie = 0;
        }
        REMOVE_ENTRY(p, e);
        return MERGE_OK;
    }
    return MERGE_FAIL;
}

/*
 * merge rules
 *
 * new(X) + del(X)                                 => remove new(X)
 * ren_fr(X) + ren_to(Y) + del(Y)                  => update ren_fr(X) to del(X), remove ren_to(Y)
 *
 * merge success, remove cur, else add to list
 */
static int merge_del_file(struct list_head* p, struct vfs_event *cur)
{
    struct vfs_event *e = list_entry(p, struct vfs_event, list);
    if (e->action < ACT_NEW_FOLDER) {
        if (cmp_event_path(e, cur))
            return MERGE_FAIL;
        REMOVE_ENTRY(p, e);
        return MERGE_OK;
    } else if (ACT_RENAME_TO_FILE == e->action) {
        /*
         * new ren_to will be paired, or update to new event
         * if ren_to is unpaired, which means it will not join merge
         */
        if (0 == e->pair){
            mpr_log("merge_del_file, unpaired ren_to event will not join merge\n");
            return MERGE_FAIL;
        }
        if (cmp_event_path(e, cur))
            return MERGE_FAIL;

        ((struct vfs_event *)e->pair)->action = ACT_DEL_FILE;
        ((struct vfs_event *)e->pair)->cookie = 0;

        REMOVE_ENTRY(p, e);
        return MERGE_OK;
    }
    return MERGE_FAIL;
}

/*
 * merge rules
 *
 * new(X) + ren_fr(X) + ren_to(Y)                  => update ren_to(Y) to new(Y), remove new(X)
 * ren_fr(X) + ren_to(Y) + ren_fr(Y) + ren_to(Z)   => update ren_fr(X) to del(X), update ren_to(Z) to new(Z), remove ren_to(Y)
 *
 * merge success, remove cur, else add to list
 */
static int merge_rename_from_file(struct list_head* p, struct vfs_event *cur)
{
    struct vfs_event *e = list_entry(p, struct vfs_event, list);
    if (e->action < ACT_NEW_FOLDER) {
        if (cmp_event_path(e, cur))
            return MERGE_FAIL;
        /*
         * ren_fr should enter buffer before ren_to, because the same thread will send ren_fr event then send ren_to event
         * at this time, ren_fr should be unpaired
         * ren_to will be update when it enter do_event_merge function
         */
        REMOVE_ENTRY(p, e);
        return MERGE_OK;
    } else if (ACT_RENAME_TO_FILE == e->action) {
        /*
         * new ren_to will be paired, or update to new event
         * if ren_to is unpaired, which means it will not join merge
         *
         * ren_to(Z) will be update when it enter do_event_merge function
         */
        if (0 == e->pair) {
            mpr_log("merge_rename_from_file, unpaired ren_to event will not join merge\n");
            return MERGE_FAIL;
        }
        if (cmp_event_path(e, cur))
            return MERGE_FAIL;

        ((struct vfs_event *)e->pair)->action = ACT_DEL_FILE;
        ((struct vfs_event *)e->pair)->cookie = 0;

        REMOVE_ENTRY(p, e);
        return MERGE_OK;
    }
    return MERGE_FAIL;
}

/*
 * merge rules
 *
 * del(X) + ren_fr(Y) + ren_to(X)                  => update ren_fr(Y) to del(Y), remove del(X)
 *
 * merge success, remove cur, else add to list
 */
static int merge_rename_to_file(struct list_head* p, struct vfs_event *cur)
{
    struct vfs_event *e = list_entry(p, struct vfs_event, list);
    if (ACT_DEL_FILE == e->action) {
        /*
         * new ren_to will be paired, or update to new event
         * if ren_to is unpaired, which means it will not join merge
         */
        if (0 == cur->pair) {
            mpr_log("merge_rename_to_file, unpaired ren_to event will not join merge\n");
            return MERGE_FAIL;
        }
        if (cmp_event_path(e, cur))
            return MERGE_FAIL;

        ((struct vfs_event *)cur->pair)->action = ACT_DEL_FILE;
        ((struct vfs_event *)cur->pair)->cookie = 0;

        REMOVE_ENTRY(p, e);
        return MERGE_OK;
    }
    return MERGE_FAIL;
}

static merge_action_fn_t action_merge_fns[] = {merge_new_file, merge_new_file, merge_new_file, 0, merge_del_file, 0, 0, 0, merge_rename_from_file, merge_rename_to_file, 0, 0, 0, 0};

/*
 * notify policy
 *      time limit, maximum time a single event is cached
 *      space limit, maximum number of event buffers
 *
 * check events after merge
 *      if event number == 0, new event already be merged, del_timer
 *      if event number == 1, mod_timer
 *      if event number > limit, del_timer and move event to local list which will be notify when level lock
 *
 * timer trigger, move event to local list which will be notify when level lock
 *
 */

#define MERGE_BUFFER_SIZE   100
#define MERGE_TIMEOUT_MS    100     /* compare with other timer, sleep, ... in this module */
#define MERGE_TIMEOUT       (HZ * MERGE_TIMEOUT_MS / 1000)
#define DUMP_SIZE           10

#define MOVE_EVENT(p, e, events_tosend) {\
    list_del(p);\
    *events_tosend++ = e; \
    --events_number; \
}

static void event_timeout_notify_callback(
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
unsigned long data
#else
struct timer_list *t
#endif
);
static DEFINE_TIMER(event_timeout_notify, event_timeout_notify_callback);

static inline void pick_events(struct vfs_event **events_tosend)
{
    struct list_head *p, *next;
    struct vfs_event *e;
    int i  = 0;

    if (unlikely(quit)) {
        list_for_each_safe(p, next, &vfs_events) {
            e = list_entry(p, struct vfs_event, list);
            MOVE_EVENT(p, e, events_tosend)
        }
    } else {
        list_for_each_safe(p, next, &vfs_events) {
            e = list_entry(p, struct vfs_event, list);
            if (e->action != ACT_RENAME_FROM_FILE || e->pair) {
                MOVE_EVENT(p, e, events_tosend)
                /* ren_fr which be moved, will unpaired with ren_to */
                if (e->action == ACT_RENAME_FROM_FILE)
                    ((struct vfs_event *)e->pair)->pair = 0;
                if (++i >= DUMP_SIZE)
                    break;
            }
        }
    }
}

static inline void notify_events(struct vfs_event **events_tosend)
{
    void *send = *events_tosend;

    if (send)
        mpr_log("notify_events\n");

    while (*events_tosend) {
        vfs_changed_entry(*events_tosend);
        vfs_event_free(*events_tosend++);
    }

    if (send && events_number >= 1) {
        mod_timer(&event_timeout_notify, jiffies + MERGE_TIMEOUT);
        mpr_log("notify_events, mod_timer\n");
    }
}

static void event_timeout_notify_callback(
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
unsigned long data
#else
struct timer_list *t
#endif
)
{
    struct vfs_event *events_tosend[MERGE_BUFFER_SIZE+1] = {0};

    mpr_log("event_timeout_notify_callback\n");

    spin_lock(&sl_vfs_events);
    pick_events(events_tosend);
    spin_unlock(&sl_vfs_events);

    notify_events(events_tosend);
}

static inline void check_events(struct vfs_event **events_tosend)
{
    if (!events_number) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
        timer_delete(&event_timeout_notify);
        mpr_log("check_events, timer_delete\n");
#else
        del_timer(&event_timeout_notify);
        mpr_log("check_events, del_timer\n");
#endif
    } else if (1 == events_number) {
        mod_timer(&event_timeout_notify, jiffies + MERGE_TIMEOUT);
        mpr_log("check_events, mod_timer\n");
    } else if (events_number >= MERGE_BUFFER_SIZE) {
        pick_events(events_tosend);
        mpr_log("check_events, pick_events\n");
    } else {
        mpr_log("check_events, events_number is %d\n", events_number);
    }
}

/*
 * not support switch merge_actions
 * merge -> not_merge: can, switch when buffer is empty
 * not_merge -> merge: can not, maybe ren_fr alread notify
 */
static int do_event_merge(struct vfs_event *event)
{
    struct list_head *p, *next;
    struct vfs_event *e;
    int merge_res = MERGE_FAIL;
    struct vfs_event *events_tosend[MERGE_BUFFER_SIZE+1] = {0};

    event->pair = 0;

    mpr_log("do_event_merge, %p, %u, %u, %s, %u\n", event, event->action, event->dev, event->path, event->cookie);

    /* disable timer softirq*/
    spin_lock_bh(&sl_vfs_events);
    /* ren_to event pairing */
    if (ACT_RENAME_TO_FILE == event->action) {
        list_for_each_entry(e, &vfs_events, list) {
            if (e->cookie == event->cookie) {
                e->pair = event;
                event->pair = e;
                mpr_log("do_event_merge, ren_to paired, %p\n", event);
                break;
            }
        }
        if (!event->pair) {
            event->action = ACT_NEW_FILE;
            event->cookie = 0;
            mpr_log("do_event_merge, ren_to -> new, %p\n", event);
        }
    }
    /* merge event */
    if (action_merge_fns[event->action]) {
        list_for_each_prev_safe(p, next, &vfs_events) {
            merge_res = action_merge_fns[event->action](p, event);
            if (MERGE_OK == merge_res)
                break;
        }
    }
    if (MERGE_OK == merge_res) {
        vfs_event_free(event);
        mpr_log("do_event_merge, merged, %p\n", event);
    } else {
        list_add_tail(&event->list, &vfs_events);
        ++events_number;
        mpr_log("do_event_merge, added, %p\n", event);
    }
    check_events(events_tosend);
    spin_unlock_bh(&sl_vfs_events);

    notify_events(events_tosend);

    return 0;
}

void *get_event_merge_entry(void *vfs_changed_func)
{
    vfs_changed_entry = vfs_changed_func;

    return do_event_merge;
}

void clearup_event_merge(void)
{
    quit = 1;

    while (events_number)
        msleep(50);
}
