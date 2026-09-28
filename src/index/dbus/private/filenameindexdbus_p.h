// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef FILENAMEINDEXDBUS_P_H
#define FILENAMEINDEXDBUS_P_H

#include "index_global.h"
#include "core/indexruntime.h"
#include "filenameindexadaptor.h"

class FileNameIndexDBus;

ANYTHING_INDEX_BEGIN_NAMESPACE

class FileNameIndexDBusPrivate
{
    friend class ::FileNameIndexDBus;

public:
    explicit FileNameIndexDBusPrivate(FileNameIndexDBus *qq)
        : q(qq),
          adapter(new FileNameIndexAdaptor(qq)),
          runtime(new IndexRuntime(IndexProfile::filename(), qq))
    {
        initialize();
        initConnect();
    }

    ~FileNameIndexDBusPrivate() { }

    void initialize();
    void initConnect();
    void handleMonitoring(bool start);
    void handleSilentStart();
    bool canSilentlyRefreshIndex(const QString &path) const;

private:
    FileNameIndexDBus *q { nullptr };
    FileNameIndexAdaptor *adapter { nullptr };
    IndexRuntime *runtime { nullptr };
};

ANYTHING_INDEX_END_NAMESPACE

#endif   // FILENAMEINDEXDBUS_P_H
