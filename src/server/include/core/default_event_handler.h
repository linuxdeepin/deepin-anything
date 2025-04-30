// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_EVENT_HANDLER_H_
#define ANYTHING_EVENT_HANDLER_H_

#include <string>
#include <vector>
#include <glib.h>
#include "core/base_event_handler.h"

ANYTHING_NAMESPACE_BEGIN

struct indexing_item {
    std::string origin_path;
    std::string event_path;
    bool different_path;
};

class default_event_handler : public base_event_handler {
public:
    explicit default_event_handler(std::shared_ptr<event_handler_config> config);
    
    void handle(fs_event event) override;

    bool is_under_indexing_path(const std::string& path, indexing_item *&indexing_item);

    void convert_event_path_to_origin_path(std::string& path, const indexing_item& item);

    bool is_event_path_blocked(const std::string& path, indexing_item *&indexing_item);

private:
    std::unordered_map<uint32_t, std::string> rename_from_;
    std::shared_ptr<event_handler_config> config_;
    std::vector<indexing_item> indexing_items_;
    std::vector<std::string> event_path_blocked_list_;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_EVENT_HANDLER_H_
