// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DEEPIN_ANYTHING_SEARCHER_H
#define DEEPIN_ANYTHING_SEARCHER_H

#include <string>
#include <vector>
#include <memory>
#include <lucene++/LuceneHeaders.h>

namespace anything {

class Searcher {
public:
    Searcher();
    ~Searcher();

    // 初始化搜索器
    bool initialize(const std::string& index_path);
    
    // 执行搜索
    std::vector<std::string> search(const std::string& path, const std::string& query, int max_results = 0);

private:
    // Lucene 相关成员
    Lucene::IndexReaderPtr reader;
    Lucene::SearcherPtr searcher;
    
    // 检查索引路径是否有效
    bool checkIndexPath(const std::string& path);
};

} // namespace anything

#endif // DEEPIN_ANYTHING_SEARCHER_H 