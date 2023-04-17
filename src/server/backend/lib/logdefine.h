// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LOGDEFINE_H
#define LOGDEFINE_H

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(logN)
Q_DECLARE_LOGGING_CATEGORY(logC)

#define nDebug(...) qCDebug(logN, __VA_ARGS__)
#define nInfo(...) qCInfo(logN, __VA_ARGS__)
#define nWarning(...) qCWarning(logN, __VA_ARGS__)
#define nCritical(...) qCCritical(logN, __VA_ARGS__)
#define cDebug(...) qCDebug(logC, __VA_ARGS__)
#define cWarning(...) qCWarning(logC, __VA_ARGS__)

#endif // LOGDEFINE_H
