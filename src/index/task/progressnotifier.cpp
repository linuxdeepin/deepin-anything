// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "progressnotifier.h"

ANYTHING_INDEX_USE_NAMESPACE

ProgressNotifier *ProgressNotifier::instance()
{
    static ProgressNotifier instance;
    return &instance;
}
