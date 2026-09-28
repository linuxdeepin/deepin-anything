// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BUILDEROPTIONS_H
#define BUILDEROPTIONS_H

#include "index_global.h"

#include <QString>

ANYTHING_INDEX_BEGIN_NAMESPACE

/**
 * @brief Optional metadata passed to document builders
 *
 * Carries supplementary information that extends the basic
 * (filePath, text) contract without polluting the virtual interface.
 * Individual builders pick only the fields they need.
 */
struct BuilderOptions
{
    QString checksum;   ///< MD5 hex digest (optional, used by OCR builder)
};

ANYTHING_INDEX_END_NAMESPACE

#endif   // BUILDEROPTIONS_H
