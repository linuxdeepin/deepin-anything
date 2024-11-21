// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_SIGNAL_HANDLER_HPP_
#define ANYTHING_SIGNAL_HANDLER_HPP_

#include <functional>

#include "common/anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

void set_signal_handler(int sig, std::function<void(int)> handler);

ANYTHING_NAMESPACE_END

#endif // ANYTHING_SIGNAL_HANDLER_HPP_