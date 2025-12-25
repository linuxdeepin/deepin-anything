// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "searcher.h"
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
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

void search_by_index(const std::string &path,
                     const std::string &query,
                     int max_results,
                     bool wildcard_query,
                     const std::string &index_path,
                     std::vector<std::string> &results)
{
    // 创建搜索器实例
    anything::Searcher searcher;
    
    // 初始化搜索器
    if (!searcher.initialize(index_path)) {
        std::cerr << "Failed to initialize searcher" << std::endl;
        return;
    }

    // 执行搜索
    results = searcher.search(path, query, max_results, wildcard_query);
}

void search_by_scan(const std::string &path,
                    const std::string &query,
                    std::vector<std::string> &results)
{
    // 在目录 path 下, 查找包含 query 字符串的文件名和目录名, 将查找结果保存到 results
    try {
        // 检查路径是否存在且为目录
        if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) {
            std::cerr << "Path does not exist or is not a directory: " << path << std::endl;
            return;
        }

        // 递归遍历目录
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                path, std::filesystem::directory_options::skip_permission_denied)) {
            
            // 获取文件名（不包含路径）
            std::string filename = entry.path().filename().string();

            if (filename.find(query) != std::string::npos) {
                // 将完整路径添加到结果中
                results.push_back(entry.path().string());
            }
        }
    } catch (const std::filesystem::filesystem_error& ex) {
        std::cerr << "Filesystem error while scanning: " << ex.what() << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "Error while scanning: " << ex.what() << std::endl;
    }
}

void search(const std::string &path,
    const std::string &query,
    int max_results,
    bool wildcard_query,
    const std::string &index_path,
    std::vector<std::string> &results)
{
    // 查看指定文件中是否包含 '"status": "updating"' 字符串
    std::string status_file = index_path + "/status.json";
    std::ifstream file(status_file);
    if (!file.is_open()) {
        std::cerr << "Fail to open status file: " << status_file << std::endl;
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    bool found = content.find("\"status\": \"updating\"") != std::string::npos;

    // 决定搜索方式
    if (found) {
        std::cout << "Index is updating, using scan search." << std::endl;
        search_by_scan(path, query, results);
    } else {
        std::cout << "Index is ready, using index search." << std::endl;
        search_by_index(path, query, max_results, wildcard_query, index_path, results);
    }
}

int main(int argc, char* argv[]) {
    std::string default_index_path = std::string(g_get_user_runtime_dir()) + "/deepin-anything-server";
    const char* index_path = default_index_path.c_str();
    g_autofree char* search_path = nullptr;
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
        search_path = g_canonicalize_filename (argv[optind], NULL);
        if (!search_path) {
            std::cerr << "Fail to canonicalize search path: " << argv[optind] << std::endl;
            return 1;
        }
        query = argv[optind + 1];
    } else {
        search_path = g_strdup("/");
        query = argv[optind];
    }

    // 执行搜索
    std::vector<std::string> results;
    search(search_path, query, 0, wildcard_query, index_path, results);

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