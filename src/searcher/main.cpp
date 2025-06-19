// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "searcher.h"
#include <iostream>
#include <string>
#include <glib.h>
#include <unistd.h>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [-i index_path] [-w] [path] <search_query>" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -i index_path  Specify the index path (default: " << std::string(g_get_user_runtime_dir()) << "/deepin-anything-server)" << std::endl;
    std::cout << "  -w             Enable wildcard search" << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << programName << " -i /path/to/index /home \"txt\"" << std::endl;
    std::cout << "  " << programName << " -w /home \"*.txt\"" << std::endl;
    std::cout << "  " << programName << " /home \"txt\"" << std::endl;
    std::cout << "  " << programName << " \"txt\" (searches in /)" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string default_index_path = std::string(g_get_user_runtime_dir()) + "/deepin-anything-server";
    const char* index_path = default_index_path.c_str();
    const char* search_path = "/";
    const char* query = nullptr;
    bool wildcard_query = false;
    int opt;

    // 处理命令行选项
    while ((opt = getopt(argc, argv, "i:w")) != -1) {
        switch (opt) {
            case 'i':
                index_path = optarg;
                break;
            case 'w':
                wildcard_query = true;
                break;
            default:
                printUsage(argv[0]);
                return 1;
        }
    }

    // 检查剩余参数
    int remaining_args = argc - optind;
    if (remaining_args < 1 || remaining_args > 2) {
        printUsage(argv[0]);
        return 1;
    }

    // 获取搜索路径和查询
    if (remaining_args == 2) {
        search_path = argv[optind];
        query = argv[optind + 1];
    } else {
        query = argv[optind];
    }

    // 创建搜索器实例
    anything::Searcher searcher;
    
    // 初始化搜索器
    if (!searcher.initialize(index_path)) {
        std::cerr << "Failed to initialize searcher" << std::endl;
        return 1;
    }

    // 执行搜索
    auto results = searcher.search(search_path, query, 0, wildcard_query);

    // 输出结果
    if (results.empty()) {
        std::cout << "No results found." << std::endl;
    } else {
        std::cout << "Found " << results.size() << " results:" << std::endl;
        for (const auto& result : results) {
            std::cout << result << std::endl;
        }
    }

    return 0;
} 