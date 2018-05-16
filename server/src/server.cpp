/*
 * Copyright (C) 2017 ~ 2018 Deepin Technology Co., Ltd.
 *
 * Author:     zccrs <zccrs@live.com>
 *
 * Maintainer: zccrs <zhangjide@deepin.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "server.h"

#include "vfs_change_uapi.h"
#include "vfs_change_consts.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QDebug>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

DAS_BEGIN_NAMESPACE

#define OnError(message) qCritical() << message << QString::fromLocal8Bit(strerror(errno)); qApp->exit(errno); return
static const char* act_names[] = {"file_created", "link_created", "symlink_created", "dir_created", "file_deleted", "dir_deleted", "file_renamed", "dir_renamed"};

Q_LOGGING_CATEGORY(vfs, "vfs", QtInfoMsg)
#define vfsInfo(...) qCInfo(vfs, __VA_ARGS__)

Server::Server(QObject *parent)
    : QThread(parent)
{

}

void Server::run()
{
    int fd = open("/proc/" PROCFS_NAME, O_RDONLY);

    if (fd < 0) {
        OnError("Failed on open: /proc/" PROCFS_NAME);
    }

    ioctl_wd_args wd;

    wd.condition_count = 500;
    wd.condition_timeout = 100;
    wd.timeout = 0;

    while (ioctl(fd, VC_IOCTL_WAITDATA, &wd) == 0) {
        vfsInfo() << "------------ begin read data ------------";

        ioctl_rs_args irsa;

        if (ioctl(fd, VC_IOCTL_READSTAT, &irsa) != 0) {
            close(fd);
            OnError("Failed on read stat");
        }

        vfsInfo() << "stat-current:" << irsa.cur_changes << ",stat-total:" << irsa.total_changes;

        if (irsa.cur_changes == 0) {
            continue;
        }

        char buf[1<<20];

        ioctl_rd_args ira ;

        ira.data = buf;
        ira.size = sizeof(buf);

        if (ioctl(fd, VC_IOCTL_READDATA, &ira) != 0) {
            OnError("Failed on read data");
        }

        vfsInfo() << "data-szie:" << ira.size;

        // no more changes
        if (ira.size == 0) {
            continue;
        }

        QByteArrayList create_list;
        QByteArrayList delete_list;
        QList<QPair<QByteArray, QByteArray>> rename_list;

        int off = 0;
        for (int i = 0; i < ira.size; i++) {
            unsigned char action = *(ira.data + off);
            ++off;
            char* src = ira.data + off, *dst = 0;
            off += strlen(src) + 1;

            switch(action) {
            case ACT_NEW_FILE:
            case ACT_NEW_SYMLINK:
            case ACT_NEW_LINK:
            case ACT_NEW_FOLDER:
                vfsInfo("%s: %s", act_names[action], src);

                create_list << src;

                if (!delete_list.isEmpty()) {
                    emit fileDeleted(delete_list);
                    delete_list.clear();
                } else if (!rename_list.isEmpty()) {
                    emit fileRenamed(rename_list);
                    rename_list.clear();
                }

                break;
            case ACT_DEL_FILE:
            case ACT_DEL_FOLDER:
                vfsInfo("%s: %s", act_names[action], src);

                delete_list << src;

                if (!create_list.isEmpty()) {
                    emit fileCreated(create_list);
                    create_list.clear();
                } else if (!rename_list.isEmpty()) {
                    emit fileRenamed(rename_list);
                    rename_list.clear();
                }

                break;
            case ACT_RENAME_FILE:
            case ACT_RENAME_FOLDER:
                dst = ira.data + off;
                off += strlen(dst) + 1;

                vfsInfo("%s: %s, %s", act_names[action], src, dst);

                rename_list << qMakePair(QByteArray(src), QByteArray(dst));

                if (!delete_list.isEmpty()) {
                    emit fileDeleted(delete_list);
                    delete_list.clear();
                } else if (!create_list.isEmpty()) {
                    emit fileCreated(create_list);
                    create_list.clear();
                }

                break;
            default:
                qWarning() << "Unknow file action" << int(action);
                break;
            }
        }

        if (!delete_list.isEmpty()) {
            emit fileDeleted(delete_list);
            delete_list.clear();
        } else if (!create_list.isEmpty()) {
            emit fileCreated(create_list);
            create_list.clear();
        } else if (!rename_list.isEmpty()) {
            emit fileRenamed(rename_list);
            rename_list.clear();
        }

        vfsInfo() << "------------ end read data ------------";
    }

    OnError("Failed on wait data");
}

DAS_END_NAMESPACE
