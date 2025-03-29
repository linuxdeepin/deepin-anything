// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/file_index_manager.h"
#include "analyzers/AnythingAnalyzer.h"

#include <chrono>
#include <filesystem>

#include "lucene++/ChineseAnalyzer.h"
#include "lucene++/Highlighter.h"
#include "lucene++/FileUtils.h"
#include "lucene++/FuzzyQuery.h"
#include "lucene++/QueryScorer.h"
#include "lucene++/SimpleHTMLFormatter.h"

#include "analyzers/AnythingAnalyzer.h"
#include "utils/log.h"
#include "utils/sys.h"
#include "utils/tools.h"
#include <glib.h>

ANYTHING_NAMESPACE_BEGIN

using namespace Lucene;

file_index_manager::file_index_manager(std::string index_dir)
    : index_directory_(std::move(index_dir)),
      pinyin_processor_("/usr/share/deepin-anything-server/pinyin.txt") {
    try {
        // DirectoryPtr dir = newLucene<RAMDirectory>();
        FSDirectoryPtr dir = FSDirectory::open(StringUtils::toUnicode(index_directory_));
        auto create = !IndexReader::indexExists(dir);
        writer_ = newLucene<IndexWriter>(dir,
            newLucene<KeywordAnalyzer>(),
            create, IndexWriter::MaxFieldLengthLIMITED);
        reader_  = IndexReader::open(dir, true);
        nrt_reader_ = writer_->getReader();
        searcher_ = newLucene<IndexSearcher>(reader_);
        nrt_searcher_ = newLucene<IndexSearcher>(nrt_reader_);
        parser_ = newLucene<QueryParser>(
            LuceneVersion::LUCENE_CURRENT, fuzzy_field_,
            newLucene<AnythingAnalyzer>());
        type_parser_ = newLucene<QueryParser>(
            LuceneVersion::LUCENE_CURRENT, type_field_,
            newLucene<AnythingAnalyzer>());
        pinyin_parser_ = newLucene<QueryParser>(
            LuceneVersion::LUCENE_CURRENT, pinyin_field_,
            newLucene<AnythingAnalyzer>());
    } catch (const LuceneException& e) {
        throw std::runtime_error("Lucene exception: " + StringUtils::toUTF8(e.getError()) +
            ". Make sure you are running the program as root.");
    }
}

file_index_manager::~file_index_manager() {
    if (writer_) {
        commit();
        writer_->close();
    }

    if (searcher_) searcher_->close();
    if (nrt_searcher_) nrt_searcher_->close();
    if (reader_) reader_->close();
    if (nrt_reader_) nrt_reader_->close();
}

void file_index_manager::add_index(const std::string& path) {
    try {
        auto doc = create_document(file_helper_.make_file_record(path));
        writer_->updateDocument(newLucene<Term>(L"full_path", StringUtils::toUnicode(path)), doc);
        spdlog::debug("Indexed {}", path);
    } catch (const LuceneException& e) {
        spdlog::error("Failed to index {}: {}", path, StringUtils::toUTF8(e.getError()));
    } catch (const std::exception& e) {
        spdlog::error(e.what());
    }
}

void file_index_manager::remove_index(const std::string& term, bool exact_match) {
    try {
        if (exact_match) {
            // Exact deletion: The field must be non-tokenized, and the term should match the full path exactly.
            TermPtr pterm = newLucene<Term>(exact_field_, StringUtils::toUnicode(term));
            writer_->deleteDocuments(pterm);
        } else {
            // Fuzzy deletion: Deletes all paths that match the specified term pattern.
            QueryPtr query = parser_->parse(StringUtils::toUnicode(term));
            writer_->deleteDocuments(query);
        }
        spdlog::debug("Removed index: {}", term);
    } catch (const LuceneException& e) {
        throw std::runtime_error("Lucene exception: " + StringUtils::toUTF8(e.getError()));
    }
}

void file_index_manager::update_index(const std::string& old_path, const std::string& new_path, bool exact_match) {
    try {
        if (exact_match) {
            // Exact updation: The old path must be a full path; otherwise, updateDocument will fail to locate and update the existing document.
            auto doc = create_document(file_helper_.make_file_record(new_path));
            writer_->updateDocument(newLucene<Term>(L"full_path", StringUtils::toUnicode(old_path)), doc);
        } else {
            // Fuzzy updation: The old path can be a file name, and all matching paths will be removed before inserting the new path.
            remove_index(old_path);
            add_index(new_path);
        }

        spdlog::debug("Renamed: {} --> {}", old_path, new_path);
    } catch (const std::exception& e) {
        spdlog::error(e.what());
    }
}

void file_index_manager::commit() {
    writer_->commit();
    spdlog::info("All changes are commited");
}

bool file_index_manager::indexed() const {
    return document_size() > 0;
}

void file_index_manager::test(const String& path) {
    spdlog::info("test path: {}", StringUtils::toUTF8(path));
    // AnalyzerPtr analyzer = newLucene<StandardAnalyzer>(LuceneVersion::LUCENE_CURRENT);
    AnalyzerPtr analyzer = newLucene<AnythingAnalyzer>();
    
    // Use a StringReader to simulate input
    TokenStreamPtr tokenStream = analyzer->tokenStream(L"", newLucene<StringReader>(path));
    // TokenPtr token = newLucene<Token>();
    
    // Tokenize and print out the results
    while (tokenStream->incrementToken()) {
        spdlog::info("Token: {}", StringUtils::toUTF8(tokenStream->toString()));
    }
}

void file_index_manager::pinyin_test(const std::string& path) {
    spdlog::info("pinyin test path: {}", path);
    auto pinyin_path = pinyin_processor_.convert_to_pinyin(path);
    test(StringUtils::toUnicode(pinyin_path));
}

int32_t file_index_manager::document_size(bool nrt) const {
    return nrt ? nrt_reader_->numDocs() : reader_->numDocs();
}

std::string file_index_manager::index_directory() const {
    return index_directory_;
}

QStringList file_index_manager::search(
    const QString& path, QString& keywords,
    int32_t offset, int32_t max_count, bool nrt) {
    spdlog::debug("Search index(path:\"{}\", keywords: \"{}\", offset: {}, max_count: {}).",
        path.toStdString(), keywords.toStdString(), offset, max_count);
    if (keywords.isEmpty()) {
        return {};
    }

    String query_terms = StringUtils::toUnicode(keywords.toStdString());

    if (keywords.at(0) == QChar('*') || keywords.at(0) == QChar('?')) {
        keywords = keywords.mid(1);
    }

    try {
        SearcherPtr searcher;
        if (nrt) {
            try_refresh_reader(true);
            searcher = nrt_searcher_;
        } else {
            try_refresh_reader();
            searcher = searcher_;
        }

        auto boolean_query = newLucene<BooleanQuery>();
        auto query = parser_->parse(StringUtils::toUnicode(keywords.toStdString()));
        boolean_query->add(query, BooleanClause::SHOULD);
        // Note that this can produce very slow queries on big indexes.
        pinyin_parser_->setAllowLeadingWildcard(true);
        auto pinyin_query = pinyin_parser_->parse(query_terms);
        auto prefix_query = newLucene<PrefixQuery>(newLucene<Term>(fuzzy_field_, query_terms));
        auto fuzzy_query = newLucene<FuzzyQuery>(newLucene<Term>(fuzzy_field_, query_terms), 0.65);
        boolean_query->add(pinyin_query, BooleanClause::SHOULD);
        boolean_query->add(prefix_query, BooleanClause::SHOULD);
        boolean_query->add(fuzzy_query, BooleanClause::SHOULD);

        auto collector = TopScoreDocCollector::create(offset + max_count, true);
        searcher->search(boolean_query, collector);

        HighlighterPtr highlighter = nullptr;
        // if (highlight) {
        //     auto scorer = newLucene<QueryScorer>(query);
        //     auto formatter = newLucene<SimpleHTMLFormatter>(L"<span style='background-color:yellow'>", L"</span>");
        //     highlighter = newLucene<Highlighter>(formatter, scorer);
        // }

        Collection<ScoreDocPtr> hits = collector->topDocs()->scoreDocs;
        if (offset >= hits.size()) {
            spdlog::debug("No more results(path:\"{}\", keyworks: \"{}\").",
                path.toStdString(), keywords.toStdString());
            return {};
        }

        QStringList results;
        auto count = std::min(offset + max_count, hits.size());
        results.reserve(count);
        for (int32_t i = offset; i < count; ++i) {
            DocumentPtr doc = searcher->doc(hits[i]->doc);
            std::filesystem::path full_path(doc->get(L"full_path"));
            QString result = QString::fromStdString(full_path.string());
            if (result.startsWith(path)) {
                results.append(result);
            }
        }

        // More results may exist; continue searching
        // if (count == max_count && !remove_list.empty()) {
        //     results.append(search(path, keywords, offset + max_count, remove_list.size(), true));
        // }

        // for (const auto& rmpath : remove_list) {
        //     remove_index(rmpath);
        // }

        return results;
    } catch (const LuceneException& e) {
        spdlog::error("Lucene exception: " + StringUtils::toUTF8(e.getError()));
        return {};
    }
}

QStringList file_index_manager::search(const QString& orginPath, QString& keywords, bool nrt) {
    QString path = orginPath;
    if (path.startsWith(g_get_home_dir()))
        path.replace(0, strlen(g_get_home_dir()), get_home_directory().c_str());

    spdlog::debug("Search index(keyworks: \"{}\").", keywords.toStdString());

    if (keywords.isEmpty()) {
        spdlog::debug("Search index(keyworks: \"{}\") finished.", keywords.toStdString());
        return {};
    }

    // 原始词条
    String query_terms = StringUtils::toUnicode(keywords.toStdString());

    // 给普通 parser 用
    if (keywords.at(0) == QChar('*') || keywords.at(0) == QChar('?')) {
        keywords = keywords.mid(1);
    }

    try {
        SearcherPtr searcher;
        int32_t max_results;
        if (nrt) {
            try_refresh_reader(true);
            searcher = nrt_searcher_;
            max_results = nrt_reader_->numDocs();
        } else {
            try_refresh_reader();
            searcher = searcher_;
            max_results = reader_->numDocs();
        }

        // auto boolean_query = newLucene<BooleanQuery>();
        // auto query = parser_->parse(StringUtils::toUnicode(keywords.toStdString()));
        // boolean_query->add(query, BooleanClause::SHOULD);
        // // Note that this can produce very slow queries on big indexes.
        // pinyin_parser_->setAllowLeadingWildcard(true);
        // auto pinyin_query = pinyin_parser_->parse(query_terms);
        // boolean_query->add(pinyin_query, BooleanClause::SHOULD);
        // AnythingAnalyzer::forEachTerm(query_terms, [this, &boolean_query](const auto& term) {
        //     boolean_query->add(newLucene<PrefixQuery>(newLucene<Term>(pinyin_field_, term)), BooleanClause::SHOULD);
        //     boolean_query->add(newLucene<FuzzyQuery>(newLucene<Term>(pinyin_field_, term), 0.55), BooleanClause::SHOULD);
        // });

        String queryString = L"*" + StringUtils::toLower(StringUtils::toUnicode(keywords.toStdString())) + L"*";
        TermPtr term = newLucene<Term>(L"file_name", queryString);
        QueryPtr query = newLucene<WildcardQuery>(term);

        auto search_results = searcher->search(query, max_results);

        HighlighterPtr highlighter = nullptr;
        // if (highlight) {
        //     auto scorer = newLucene<QueryScorer>(query);
        //     auto formatter = newLucene<SimpleHTMLFormatter>(L"<span style='background-color:yellow'>", L"</span>");
        //     highlighter = newLucene<Highlighter>(formatter, scorer);
        // }

        QStringList results;
        results.reserve(search_results->scoreDocs.size());
        for (const auto& score_doc : search_results->scoreDocs) {
            DocumentPtr doc = searcher->doc(score_doc->doc);
            auto result = QString::fromStdWString(doc->get(L"full_path")
                + L"<\\>" + doc->get(L"file_type")
                + L"<\\>" + doc->get(L"file_ext")
                + L"<\\>" + doc->get(L"modify_time_str")
                + L"<\\>" + doc->get(L"file_size_str"));
            if (result.startsWith(path)) {
                results.append(std::move(result));
            }
        }

        spdlog::debug("Search index(keyworks: \"{}\") finished.", keywords.toStdString());
        return results;
    } catch (const LuceneException& e) {
        spdlog::error("Lucene exception: " + StringUtils::toUTF8(e.getError()));
        spdlog::debug("Search index(keyworks: \"{}\") finished.", keywords.toStdString());
        return {};
    }
}

QStringList file_index_manager::search(const QString& path, QString& keywords, const QString& type, bool nrt) {
    spdlog::debug("Search index(keyworks: \"{}\", type: \"{}\").", keywords.toStdString(), type.toStdString());
    if (keywords.isEmpty() && type.isEmpty()) {
        return {};
    } else if (type.isEmpty()) {
        return search(path, keywords, nrt);
    }

    String query_terms = StringUtils::toUnicode(keywords.toStdString());

    if (keywords.at(0) == QChar('*') || keywords.at(0) == QChar('?')) {
        keywords = keywords.mid(1);
    }

    try {
        SearcherPtr searcher;
        int32_t max_results;
        if (nrt) {
            try_refresh_reader(true);
            searcher = nrt_searcher_;
            max_results = nrt_reader_->numDocs();
        } else {
            try_refresh_reader();
            searcher = searcher_;
            max_results = reader_->numDocs();
        }

        QueryPtr type_query = type_parser_->parse(StringUtils::toUnicode(type.toStdString()));
        auto boolean_query = newLucene<BooleanQuery>();
        boolean_query->add(type_query, BooleanClause::MUST);
        if (!keywords.isEmpty()) {
            auto sub_boolean_query = newLucene<BooleanQuery>();
            auto query = parser_->parse(StringUtils::toUnicode(keywords.toStdString()));
            sub_boolean_query->add(query, BooleanClause::SHOULD);
            // Note that this can produce very slow queries on big indexes.
            pinyin_parser_->setAllowLeadingWildcard(true);
            auto pinyin_query = pinyin_parser_->parse(query_terms);
            sub_boolean_query->add(pinyin_query, BooleanClause::SHOULD);
            AnythingAnalyzer::forEachTerm(query_terms, [this, &sub_boolean_query](const auto& term) {
                sub_boolean_query->add(newLucene<PrefixQuery>(newLucene<Term>(pinyin_field_, term)), BooleanClause::SHOULD);
                sub_boolean_query->add(newLucene<FuzzyQuery>(newLucene<Term>(pinyin_field_, term), 0.65), BooleanClause::SHOULD);
            });
            sub_boolean_query->setMinimumNumberShouldMatch(1);
            boolean_query->add(sub_boolean_query, BooleanClause::MUST);
        }

        auto search_results = searcher->search(boolean_query, max_results);
        
        QStringList results;
        results.reserve(search_results->scoreDocs.size());
        for (const auto& score_doc : search_results->scoreDocs) {
            DocumentPtr doc = searcher->doc(score_doc->doc);
            QString result = QString::fromStdWString(doc->get(L"full_path"));
            if (result.startsWith(path)) {
                results.append(std::move(result));
            }
        }

        return results;
    } catch (const LuceneException& e) {
        spdlog::error("Lucene exception: " + StringUtils::toUTF8(e.getError()));
        return {};
    }

    return {};
}

QStringList file_index_manager::search(const QString& path, QString& keywords,
    const QString& after, const QString& before, bool nrt) {
    spdlog::debug("Search index(keywords: \"{}\", after: \"{}\", before: \"{}\").",
        keywords.toStdString(), after.toStdString(), before.toStdString());
    if (keywords.isEmpty()) {
        return {};
    }

    if (after.isEmpty() && before.isEmpty()) {
        return search(path, keywords, nrt);
    }

    String query_terms = StringUtils::toUnicode(keywords.toStdString());

    try {
        SearcherPtr searcher;
        int32_t max_results;
        if (nrt) {
            try_refresh_reader(true);
            searcher = nrt_searcher_;
            max_results = nrt_reader_->numDocs();
        } else {
            try_refresh_reader();
            searcher = searcher_;
            max_results = reader_->numDocs();
        }

        int64_t after_timestamp = 0;
        int64_t before_timestamp = std::numeric_limits<int64_t>::max();
        if (!after.isEmpty()) {
            after_timestamp = file_helper_.to_milliseconds_since_epoch(after.toStdString());
        }
        if (!before.isEmpty()) {
            before_timestamp = file_helper_.to_milliseconds_since_epoch(before.toStdString());
        }

        auto boolean_query = newLucene<BooleanQuery>();
        auto query = parser_->parse(query_terms);
        auto time_query = NumericRangeQuery::newLongRange(L"modify_time", after_timestamp, before_timestamp, true, true);
        boolean_query->add(time_query, BooleanClause::MUST);

        auto sub_boolean_query = newLucene<BooleanQuery>();
        sub_boolean_query->add(query, BooleanClause::SHOULD);
        auto pinyin_query = pinyin_parser_->parse(query_terms);
        sub_boolean_query->add(pinyin_query, BooleanClause::SHOULD);
        AnythingAnalyzer::forEachTerm(query_terms, [this, &sub_boolean_query](const auto& term) {
            sub_boolean_query->add(newLucene<PrefixQuery>(newLucene<Term>(pinyin_field_, term)), BooleanClause::SHOULD);
            sub_boolean_query->add(newLucene<FuzzyQuery>(newLucene<Term>(pinyin_field_, term), 0.65), BooleanClause::SHOULD);
        });

        sub_boolean_query->setMinimumNumberShouldMatch(1);
        boolean_query->add(sub_boolean_query, BooleanClause::MUST);

        auto search_results = searcher->search(boolean_query, max_results);

        QStringList results;
        results.reserve(search_results->scoreDocs.size());
        for (const auto& score_doc : search_results->scoreDocs) {
            DocumentPtr doc = searcher->doc(score_doc->doc);
            QString result = QString::fromStdWString(doc->get(L"full_path"));
            if (result.startsWith(path)) {
                results.append(std::move(result));
            }
        }

        return results;
    } catch (const LuceneException& e) {
        spdlog::error("Lucene exception: " + StringUtils::toUTF8(e.getError()));
    } catch (const std::exception& e) {
        spdlog::error(e.what());
    }

    return {};
}

void file_index_manager::async_search(QString& keywords, bool nrt,
    std::function<void(const QStringList&)> callback) {
    spdlog::debug("Async search index(keyworks: \"{}\").", keywords.toStdString());
    if (keywords.isEmpty()) {
        return;
    }

    String query_terms = StringUtils::toUnicode(keywords.toStdString());
    if (keywords.at(0) == QChar('*')) {
        keywords = keywords.mid(1);
    }

    search_cancelled_ = true;
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(50ms);
    search_cancelled_ = false;

    std::thread([this, nrt, keywords = std::move(keywords), query_terms = std::move(query_terms), callback = std::move(callback)]() {
        try {
            SearcherPtr searcher;
            int32_t max_results;
            if (nrt) {
                try_refresh_reader(true);
                searcher = nrt_searcher_;
                max_results = nrt_reader_->numDocs();
            } else {
                try_refresh_reader();
                searcher = searcher_;
                max_results = reader_->numDocs();
            }

            auto boolean_query = newLucene<BooleanQuery>();
            auto query = parser_->parse(StringUtils::toUnicode(keywords.toStdString()));
            boolean_query->add(query, BooleanClause::SHOULD);
            // Note that this can produce very slow queries on big indexes.
            pinyin_parser_->setAllowLeadingWildcard(true);
            auto pinyin_query = pinyin_parser_->parse(query_terms);
            boolean_query->add(pinyin_query, BooleanClause::SHOULD);

            AnythingAnalyzer::forEachTerm(query_terms, [this, &boolean_query](const auto& term) {
                boolean_query->add(newLucene<PrefixQuery>(newLucene<Term>(pinyin_field_, term)), BooleanClause::SHOULD);
                boolean_query->add(newLucene<FuzzyQuery>(newLucene<Term>(pinyin_field_, term), 0.55), BooleanClause::SHOULD);
            });

            auto search_results = searcher->search(boolean_query, max_results);
            QStringList results;
            results.reserve(search_results->scoreDocs.size());
            for (const auto& score_doc : search_results->scoreDocs) {
                if (search_cancelled_) {
                    spdlog::debug("Search cancelled: {}", keywords.toStdString());
                    return;
                }

                DocumentPtr doc = searcher->doc(score_doc->doc);
                auto full_path = doc->get(L"full_path");

                // Since there is no automatic cleanup for invalid indexes,
                // the system may contain paths that no longer exist.
                // To maintain index validity, invalid indexes are filtered and removed here.
                if (!std::filesystem::exists(full_path)) {
                    remove_index(StringUtils::toUTF8(full_path));
                    continue;
                }

                results.append(QString::fromStdWString(full_path));
            }

            callback(results);
        } catch (const LuceneException& e) {
            spdlog::error("Lucene exception: " + StringUtils::toUTF8(e.getError()));
        }
    }).detach();
}

QStringList file_index_manager::traverse_directory(const QString& path, bool nrt) {
    if (path.isEmpty()) {
        return {};
    }

    String query_terms = StringUtils::toUnicode(path.toStdString());

    try {
        SearcherPtr searcher;
        int32_t max_results;
        if (nrt) {
            try_refresh_reader(true);
            searcher = nrt_searcher_;
            max_results = nrt_reader_->numDocs();
        } else {
            try_refresh_reader();
            searcher = searcher_;
            max_results = reader_->numDocs();
        }

        auto query = newLucene<PrefixQuery>(newLucene<Term>(exact_field_, query_terms));
        auto search_results = searcher->search(query, max_results);

        QStringList results;
        results.reserve(search_results->scoreDocs.size());
        for (const auto& score_doc : search_results->scoreDocs) {
            DocumentPtr doc = searcher->doc(score_doc->doc);
            results.append(QString::fromStdWString(doc->get(L"full_path")));
        }

        return results;
    } catch (const LuceneException& e) {
        spdlog::error("Lucene exception: " + StringUtils::toUTF8(e.getError()));
        return {};
    }
}

bool file_index_manager::document_exists(const std::string &path, bool only_check_initial_index) {
    TermPtr term = newLucene<Term>(L"full_path", StringUtils::toUnicode(path));
    TermDocsPtr termDocs;
    if (only_check_initial_index) {
        termDocs = reader_->termDocs(term);
    } else {
        try_refresh_reader(true);
        termDocs = nrt_reader_->termDocs(term);
    }
    return termDocs->next();
}

void file_index_manager::refresh_indexes() {
    spdlog::info("Refreshing file indexes...");
    try_refresh_reader();
    auto query = newLucene<Lucene::MatchAllDocsQuery>();
    auto num_docs = reader_->numDocs();
    if (num_docs > 0) {
        auto search_results = searcher_->search(query, num_docs);
        std::vector<std::string> remove_list;
        std::vector<std::string> update_list;
        for (const auto& score_doc : search_results->scoreDocs) {
            DocumentPtr doc = searcher_->doc(score_doc->doc);
            std::filesystem::path full_path(doc->get(L"full_path"));
            if (!std::filesystem::exists(full_path)) {
                remove_list.push_back(full_path.string());
            } /*else {
                auto last_write_time = file_helper_.get_file_last_write_time(full_path);
                std::filesystem::path current_write_time(doc->get(L"last_write_time"));
                if (last_write_time != current_write_time.string()) {
                    update_list.push_back(full_path.string());
                }
            }*/
        }

        for (const auto& path : remove_list) {
            // Remove non-existent path from the indexes
            remove_index(path);
        }
        for (const auto& path : update_list) {
            // Update the basic information for the indexed file
            add_index(path);
        }
    }
}

void file_index_manager::try_refresh_reader(bool nrt) {
    std::lock_guard<std::mutex> lock(reader_mtx_);
    if (nrt) {
        if (!nrt_reader_->isCurrent()) {
            IndexReaderPtr new_reader = writer_->getReader();
            if (new_reader != nrt_reader_) {
                nrt_reader_->close();
                nrt_reader_ = new_reader;
                nrt_searcher_ = newLucene<IndexSearcher>(nrt_reader_);
            }
        }
    } else {
        if (!reader_->isCurrent()) {
            IndexReaderPtr new_reader = reader_->reopen();
            if (new_reader != reader_) {
                reader_->close();
                reader_ = new_reader;
                searcher_ = newLucene<IndexSearcher>(reader_);
            }
        }
    }
}

DocumentPtr file_index_manager::create_document(const file_record& record) {
    DocumentPtr doc = newLucene<Document>();
    // File name with fuzzy match; parser is required for searching and deleting.
    doc->add(newLucene<Field>(L"file_name",
        StringUtils::toLower(StringUtils::toUnicode(record.file_name)),
        Field::STORE_YES, Field::INDEX_ANALYZED));
    // Full path with exact match; parser is not needed for deleting and exact searching, which improves perferemce.
    doc->add(newLucene<Field>(L"full_path",
        StringUtils::toUnicode(record.full_path),
        Field::STORE_YES, Field::INDEX_NOT_ANALYZED));
    doc->add(newLucene<Field>(L"file_type",
        StringUtils::toUnicode(record.file_type),
        Field::STORE_YES, Field::INDEX_ANALYZED));
    doc->add(newLucene<Field>(L"file_ext",
        StringUtils::toUnicode(record.file_ext),
        Field::STORE_YES, Field::INDEX_ANALYZED));

    char *formatted_time = format_time(record.modify_time);
    doc->add(newLucene<Field>(L"modify_time_str",
        StringUtils::toUnicode(formatted_time),
        Field::STORE_YES, Field::INDEX_NOT_ANALYZED));
    g_free(formatted_time);

    char *formatted_size = format_size(record.file_size);
    doc->add(newLucene<Field>(L"file_size_str",
        StringUtils::toUnicode(formatted_size),
        Field::STORE_YES, Field::INDEX_NOT_ANALYZED));
    g_free(formatted_size);

    doc->add(newLucene<NumericField>(L"modify_time")->setLongValue(record.modify_time));
    doc->add(newLucene<NumericField>(L"file_size")->setLongValue(record.file_size));
    // spdlog::info("pinyin: {}", pinyin_processor_.convert_to_pinyin(record.file_name));
    doc->add(newLucene<Field>(L"pinyin",
        StringUtils::toUnicode(pinyin_processor_.convert_to_pinyin(record.file_name)),
        Field::STORE_YES, Field::INDEX_ANALYZED));
    return doc;
}

ANYTHING_NAMESPACE_END
