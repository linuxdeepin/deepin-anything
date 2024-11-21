// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_EVENT_HANDLER_H_
#define ANYTHING_EVENT_HANDLER_H_

#include "core/base_event_handler.h"
#include "utils/sys.h"

ANYTHING_NAMESPACE_BEGIN

class default_event_handler : public base_event_handler {
public:
    explicit default_event_handler(std::string index_dir =
        get_sys_cache_directory() + "/deepin-anything-dxnu-version");
    
    void handle(fs_event event) override;

private:
    std::unordered_map<uint32_t, std::string> rename_from_;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_EVENT_HANDLER_H_