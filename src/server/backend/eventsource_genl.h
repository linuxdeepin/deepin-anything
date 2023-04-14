// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EVENTSOURCE_GENL_H
#define EVENTSOURCE_GENL_H

#include <QMap>
#include <QByteArray>
#include "dasdefine.h"
#include "eventsource.h"

struct nl_msg;
struct nl_sock;
struct nl_cb;

DAS_BEGIN_NAMESPACE

class EventSource_GENL : public EventSource
{
public:
    EventSource_GENL();
    ~EventSource_GENL() override;

    bool init() override;
    bool isInited() override;
    bool getEvent(unsigned char *type, char **src, char **dst, bool *end) override;

private:
    void updatePartitions();
    static int handleMsg(struct nl_msg *msg, void* arg);
    int handleMsg(struct nl_msg *msg);

    bool saveData(unsigned char _act, char *_root, char *_src, char *_dst);

private:
    bool inited;

    struct nl_sock *nlsock;
    struct nl_cb *cb;
    QMap<unsigned int, QByteArray> rename_from;
    QMap<unsigned int, QByteArray> partitions;

    char buf[4096*2];
    bool new_msg;
    unsigned char act;
    char *dst;
};

DAS_END_NAMESPACE

#endif // EVENTSOURCE_VFSCHANGE_H
