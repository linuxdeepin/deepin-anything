// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CONTENTDEDUPLICATION_H
#define CONTENTDEDUPLICATION_H

#include "index_global.h"

#include <QString>

ANYTHING_INDEX_BEGIN_NAMESPACE

namespace ContentDeduplication {

/**
 * @brief Attempt to find existing content text by file checksum
 *
 * Opens the content index read-only, searches for a document with matching
 * checksum, and returns the contents text if found.
 *
 * This is a best-effort optimization: on any failure (index unavailable,
 * corrupt, etc.) it returns an empty string so normal content extraction proceeds.
 *
 * @param checksum  MD5 hex digest of the file
 * @param contentIndexDir  Path to the content Lucene index directory
 * @return The content text if a matching document was found, empty string otherwise
 */
QString lookupByTextChecksum(const QString &checksum, const QString &contentIndexDir);

}   // namespace ContentDeduplication

ANYTHING_INDEX_END_NAMESPACE

#endif   // CONTENTDEDUPLICATION_H