// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "serviceentry.h"

#include "dbus/textindexdbus.h"
#include "dbus/ocrindexdbus.h"
#include "dbus/filenameindexdbus.h"

#include "processprioritymanager.h"

#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMetaType>
#include <QHash>
#include <QDebug>

namespace anything_index {

static TextIndexDBus *textIndexDBus = nullptr;
static OcrIndexDBus *ocrIndexDBus = nullptr;
static FileNameIndexDBus *fileNameIndexDBus = nullptr;

int registerIndexServices()
{
    // ProcessFileMoves 参数类型未注册时 QtDBus 会剔除该方法（a{ss} 线格式不变）
    qDBusRegisterMetaType<QHash<QString, QString>>();

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(Defines::kTextIndexDBusService)
        && bus.lastError().type() != QDBusError::NoError) {
        qWarning() << "deepin-anything-index: failed to register text index DBus service:" << bus.lastError().message();
    }

    if (!bus.registerService(Defines::kOcrIndexDBusService)
        && bus.lastError().type() != QDBusError::NoError) {
        qWarning() << "deepin-anything-index: failed to register OCR index DBus service:" << bus.lastError().message();
    }

    if (!bus.registerService(Defines::kFileNameIndexDBusService)
        && bus.lastError().type() != QDBusError::NoError) {
        qWarning() << "deepin-anything-index: failed to register filename index DBus service:" << bus.lastError().message();
    }

    textIndexDBus = new TextIndexDBus();
    ocrIndexDBus = new OcrIndexDBus();
    fileNameIndexDBus = new FileNameIndexDBus();
    ProcessPriorityManager::lowerAllAvailablePriorities(true);

    return 0;
}

int unregisterIndexServices()
{
    if (fileNameIndexDBus) {
        fileNameIndexDBus->cleanup();
        fileNameIndexDBus->deleteLater();
        fileNameIndexDBus = nullptr;
    }

    if (ocrIndexDBus) {
        ocrIndexDBus->cleanup();
        ocrIndexDBus->deleteLater();
        ocrIndexDBus = nullptr;
    }

    if (textIndexDBus) {
        textIndexDBus->cleanup();
        textIndexDBus->deleteLater();
        textIndexDBus = nullptr;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.unregisterService(Defines::kFileNameIndexDBusService);
    bus.unregisterService(Defines::kOcrIndexDBusService);
    bus.unregisterService(Defines::kTextIndexDBusService);
    return 0;
}

}   // namespace anything_index
