// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef INDEXTRAVERSEUTILS_H
#define INDEXTRAVERSEUTILS_H

#include "index_global.h"

#include <QString>
#include <QSet>

ANYTHING_INDEX_BEGIN_NAMESPACE

namespace IndexTraverseUtils {

// 检查是否是隐藏文件
bool isHiddenFile(const char *name);

// 检查是否是特殊目录(. 或 ..)
bool isSpecialDir(const char *name);

// 检查文件路径是否有效
bool isValidFile(const QString &path);

// 检查目录是否有效且未被访问过
bool isValidDirectory(const QString &path, QSet<QString> &visitedDirs);

// 检查是否应该跳过该目录
bool shouldSkipDirectory(const QString &path);

QMap<QString, QString> fstabBindInfo();

}   // namespace IndexTraverseUtils

ANYTHING_INDEX_END_NAMESPACE

#endif   // INDEXTRAVERSEUTILS_H
