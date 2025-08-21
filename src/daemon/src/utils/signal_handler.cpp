// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "utils/signal_handler.h"

#include <csignal>
#include <unordered_map>

ANYTHING_NAMESPACE_BEGIN

namespace {

std::unordered_map<int, std::function<void(int)>> signal_map;
void default_signal_handler(int signal) {
    if (auto it = signal_map.find(signal); it != signal_map.end())
        it->second(signal);
}

}

void set_signal_handler(int sig, std::function<void(int)> handler) {
    if (signal_map.count(sig) == 0) {
        std::signal(sig, default_signal_handler);
    }

    signal_map[sig] = std::move(handler);
}

ANYTHING_NAMESPACE_END