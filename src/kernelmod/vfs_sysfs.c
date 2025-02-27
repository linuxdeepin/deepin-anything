// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include "vfs_sysfs.h"
#include "vfs_log.h"


static struct kobject *vfs_monitor;

char vfs_unnamed_devices[MAX_MINOR+1];

#define MAX_INPUT_MINOR (MAX_MINOR+1)

static ssize_t vfs_unnamed_devices_show(struct kobject *kobj,
                            struct kobj_attribute *attr, char *buf)
{
    /* the size of buf is PAGE_SIZE, typically 4096 bytes, That's enough for us */
    char *pbuf = buf;
    int first = 1;

    pbuf[0] = '\0';
    for (int i = 0; i <= MAX_MINOR; ++i) {
        if (!vfs_unnamed_devices[i])
            continue;

        if (first) {
            first = 0;
            sprintf(pbuf, "%d", i);
        }
        else {
            sprintf(pbuf, ",%d", i);
        }
        
        /* move to pbuf tail */
        while (*(++pbuf)) ;
    }
    pbuf[0] = '\n';
    pbuf[1] = '\0';

    return strlen(buf);
}

/*
 * aN: set vfs_unnamed_devices[N] to 1
 * rN: set vfs_unnamed_devices[N] to 0
 * eN: set vfs_unnamed_devices[*] to 0
 * N in [0, 255]
 */
static ssize_t vfs_unnamed_devices_store(struct kobject *kobj,
                                struct kobj_attribute *attr, char *buf,
                                size_t count)
{
    char act;
    unsigned char minor;
    int ret;

    ret = sscanf(buf, "%c%hhu", &act, &minor);
    if (ret != 2)
        return -EINVAL;

    switch (act) {
    case 'e':
        memset (vfs_unnamed_devices, 0, sizeof(vfs_unnamed_devices));
        break;
    case 'a':
        vfs_unnamed_devices[minor] = 1;
        break;
    case 'r':
        vfs_unnamed_devices[minor] = 0;
        break;
    default:
        return -EINVAL;
    }

    return count;
}

static struct kobj_attribute vfs_unnamed_devices_attribute =
    __ATTR(vfs_unnamed_devices, 0660, vfs_unnamed_devices_show, (void *)vfs_unnamed_devices_store);

int vfs_init_sysfs(void)
{
    int error = 0;

    vfs_monitor = kobject_create_and_add("vfs_monitor", kernel_kobj);
    if (!vfs_monitor)
        return -ENOMEM;

    error = sysfs_create_file(vfs_monitor,
        &vfs_unnamed_devices_attribute.attr);
    if (error) {
        mpr_info("failed to create the vfs_unnamed_devices file "
                "in /sys/kernel/vfs_monitor\n");
    }

    return error;
}

void vfs_exit_sysfs(void)
{
    kobject_put(vfs_monitor);
}