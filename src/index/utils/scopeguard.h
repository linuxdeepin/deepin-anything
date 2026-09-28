// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCOPEGUARD_H
#define SCOPEGUARD_H

#include "index_global.h"

#include <functional>

ANYTHING_INDEX_BEGIN_NAMESPACE

class ScopeGuard
{
public:
    explicit ScopeGuard(std::function<void()> onExit)
        : m_exitFunc(std::move(onExit)), m_dismissed(false) { }

    ~ScopeGuard()
    {
        if (!m_dismissed && m_exitFunc)
            m_exitFunc();
    }

    void dismiss() { m_dismissed = true; }

private:
    std::function<void()> m_exitFunc;
    bool m_dismissed;
};

ANYTHING_INDEX_END_NAMESPACE

#endif   // SCOPEGUARD_H
