// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filehash.h"

#include <QCryptographicHash>
#include <QFile>

ANYTHING_INDEX_BEGIN_NAMESPACE

namespace FileHash {

QString computeMd5(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Md5);
    if (!hash.addData(&file)) {
        return {};
    }

    return QString::fromLatin1(hash.result().toHex());
}

}   // namespace FileHash

ANYTHING_INDEX_END_NAMESPACE
