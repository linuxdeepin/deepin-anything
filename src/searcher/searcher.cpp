// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "searcher.h"
#include "analyzers/chineseanalyzer.h"
#include <glib.h>
#include <iostream>

using namespace Lucene;

namespace Anything {

Searcher::Searcher() {
}

Searcher::~Searcher() {
    // 清理 Lucene 资源
    if (reader) {
        reader->close();
    }
}

bool Searcher::initialize(const std::string& index_path) {
    if (!checkIndexPath(index_path)) {
        std::cerr << "Invalid index path: " << index_path << std::endl;
        return false;
    }

    try {
        // 打开索引目录
        FSDirectoryPtr dir = FSDirectory::open(StringUtils::toUnicode(index_path.c_str()));
        reader = IndexReader::open(dir, true);
        searcher = newLucene<IndexSearcher>(reader);
        return true;
    } catch (const LuceneException& e) {
        std::cerr << "Failed to initialize searcher: " << StringUtils::toUTF8(e.getError()) << std::endl;
        return false;
    }
}

std::vector<std::string> Searcher::search(const std::string& path, const std::string& query, int max_results) {
    std::vector<std::string> results;
    
    if (!searcher) {
        std::cerr << "Searcher not initialized" << std::endl;
        return results;
    }

    try {
        // 创建查询解析器
        AnalyzerPtr analyzer = newLucene<ChineseAnalyzer>();
        QueryParserPtr parser = newLucene<QueryParser>(LuceneVersion::LUCENE_CURRENT, L"file_name", analyzer);
        
        // 解析查询
        QueryPtr query_ptr = parser->parse(StringUtils::toLower(StringUtils::toUnicode(query.c_str())));
        
        // 执行搜索
        if (max_results == 0) {
            max_results = reader->numDocs();
        }
        std::cout << "Doc num: " << reader->numDocs() << std::endl;
        TopDocsPtr topDocs = searcher->search(query_ptr, max_results);
        
        // 处理搜索结果
        for (int32_t i = 0; i < topDocs->totalHits; ++i) {
            ScoreDocPtr scoreDoc = topDocs->scoreDocs[i];
            DocumentPtr doc = searcher->doc(scoreDoc->doc);
            std::stringstream ss;
            ss << StringUtils::toUTF8(doc->get(L"full_path"))
                << "<\\>" << StringUtils::toUTF8(doc->get(L"file_type"))
                << "<\\>" << StringUtils::toUTF8(doc->get(L"file_ext"))
                << "<\\>" << StringUtils::toUTF8(doc->get(L"modify_time_str"))
                << "<\\>" << StringUtils::toUTF8(doc->get(L"file_size_str"))
                << "<\\>" << StringUtils::toUTF8(doc->get(L"pinyin"))
                << "<\\>" << StringUtils::toUTF8(doc->get(L"is_hidden"));
            std::string result = ss.str();
            if (result.rfind(path, 0) == 0) {
                results.push_back(result);
            }
        }
    } catch (const LuceneException& e) {
        std::cerr << "Search failed: " << StringUtils::toUTF8(e.getError()) << std::endl;
    }

    return results;
}

bool Searcher::checkIndexPath(const std::string& path) {
    return g_file_test(path.c_str(), G_FILE_TEST_IS_DIR);
}

} // namespace Anything 