// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/file_index_manager.h"

#include <filesystem>
#include <iostream>

#include "lucene++/FileUtils.h"
#include "lucene++/ChineseAnalyzer.h"

#include "utils/log.h"
#include "utils/string_helper.h"

ANYTHING_NAMESPACE_BEGIN

using namespace Lucene;

file_index_manager::file_index_manager(std::string index_dir)
    : index_directory_(std::move(index_dir)) {
    try {
        FSDirectoryPtr dir = FSDirectory::open(StringUtils::toUnicode(index_directory_));
        auto create = !IndexReader::indexExists(dir);
        writer_ = newLucene<IndexWriter>(dir,
            newLucene<ChineseAnalyzer>(),
            create, IndexWriter::MaxFieldLengthLIMITED);
        reader_  = IndexReader::open(dir, true);
        nrt_reader_ = writer_->getReader();
        searcher_ = newLucene<IndexSearcher>(reader_);
        nrt_searcher_ = newLucene<IndexSearcher>(nrt_reader_);
        parser_ = newLucene<QueryParser>(
            LuceneVersion::LUCENE_CURRENT, fuzzy_field_,
            newLucene<ChineseAnalyzer>());
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
        auto doc = create_document(file_helper::make_file_record(path));
        writer_->updateDocument(newLucene<Term>(L"full_path", StringUtils::toUnicode(path)), doc);
        std::lock_guard<std::mutex> lock(mtx_);
        log::debug() << "Indexed " << path << "\n";
    } catch (const LuceneException& e) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            log::error() << "Failed to index " << path << ": " << StringUtils::toUTF8(e.getError()) << "\n";
        }
        throw std::runtime_error("Lucene exception: " + StringUtils::toUTF8(e.getError()));
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
        std::lock_guard<std::mutex> lock(mtx_);
        log::debug() << "Removed index: " << term << "\n";
    } catch (const LuceneException& e) {
        throw std::runtime_error("Lucene exception: " + StringUtils::toUTF8(e.getError()));
    }
}

void file_index_manager::update_index(
    const std::string& old_path,
    const std::string& new_path,
    bool exact_match) {
    if (exact_match) {
        // Exact updation: The old path must be a full path; otherwise, updateDocument will fail to locate and update the existing document.
        auto doc = create_document(file_helper::make_file_record(new_path));
        writer_->updateDocument(newLucene<Term>(L"full_path", StringUtils::toUnicode(old_path)), doc);
    } else {
        // Fuzzy updation: The old path can be a file name, and all matching paths will be removed before inserting the new path.
        remove_index(old_path);
        add_index(new_path);
    }

    std::lock_guard<std::mutex> lock(mtx_);
    log::debug() << "Renamed: " << old_path << " --> " << new_path << "\n";
}

// std::vector<file_record> file_index_manager::search_index(const std::string& term, bool exact_match, bool nrt) {
//     std::vector<file_record> results;
//     TopScoreDocCollectorPtr collector = search(term, exact_match, nrt);

//     if (collector->getTotalHits() == 0) {
//         log::info() << "Found no files\n";
//         return results;
//     }

//     SearcherPtr searcher = nrt ? nrt_searcher_ : searcher_;
//     Collection<ScoreDocPtr> hits = collector->topDocs()->scoreDocs;
//     for (int32_t i = 0; i < hits.size(); ++i) {
//         DocumentPtr doc = searcher->doc(hits[i]->doc);
//         file_record record;
//         String file_name = doc->get(L"file_name");
//         String full_path = doc->get(L"full_path");
//         String last_write_time = doc->get(L"last_write_time");
//         if (!file_name.empty()) record.file_name = StringUtils::toUTF8(file_name);
//         if (!full_path.empty()) record.full_path = StringUtils::toUTF8(full_path);
//         // std::cout << "------------------\n";
//         // std::cout << "full_path: " << record.full_path << "\n";
//         // std::cout << "modified: " << record.modified << "\n";
//         // std::cout << "------------------\n";
//         results.push_back(std::move(record));
//     }

//     return results;
// }

void file_index_manager::commit() {
    writer_->commit();
    log::info() << "All changes are commited\n";
}

bool file_index_manager::indexed() const {
    return document_size() > 0;
}

void file_index_manager::test(const String& path) {
    log::info() << "test path: " << StringUtils::toUTF8(path) << "\n";
    AnalyzerPtr analyzer = newLucene<ChineseAnalyzer>(); // newLucene<jieba_analyzer>();
    
    // Use a StringReader to simulate input
    TokenStreamPtr tokenStream = analyzer->tokenStream(L"", newLucene<StringReader>(path));
    TokenPtr token = newLucene<Token>();
    
    // Tokenize and print out the results
    while (tokenStream->incrementToken()) {
        log::info() << "Token: " << StringUtils::toUTF8(tokenStream->toString()) << "\n";
    }
}

int file_index_manager::document_size(bool nrt) const {
    return nrt ? nrt_reader_->numDocs() : reader_->numDocs();
}

std::string file_index_manager::index_directory() const {
    return index_directory_;
}

QStringList file_index_manager::search(
    const QString& path, const QString& keyword,
    int32_t offset, int32_t max_count, bool nrt) {
    log::debug() << "Search index(path:\"" << path.toStdString() << "\", keywork: \"" << keyword.toStdString() << "\").\n";
    if (keyword.isEmpty()) {
        return {};
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

        QueryPtr query = parser_->parse(StringUtils::toUnicode(keyword.toStdString()));
        TopScoreDocCollectorPtr collector = TopScoreDocCollector::create(offset + max_count, true);
        searcher->search(query, collector);

        Collection<ScoreDocPtr> hits = collector->topDocs()->scoreDocs;
        if (offset >= hits.size()) {
            log::debug() << "No more results(path:\"" << path.toStdString() << "\", keywork: \"" << keyword.toStdString() << "\").\n";
            return {};
        }

        QStringList results;
        max_count = std::min(offset + max_count, hits.size());
        results.reserve(max_count);
        for (int32_t i = offset; i < max_count; ++i) {
            DocumentPtr doc = searcher->doc(hits[i]->doc);
            QString full_path = QString::fromStdWString(doc->get(L"full_path"));
            if (full_path.startsWith(path)) {
                results.append(full_path);
            }
        }

        return results;
    } catch (const LuceneException& e) {
        throw std::runtime_error("Lucene exception: " + StringUtils::toUTF8(e.getError()));
    }
}

QStringList file_index_manager::search(const QString& keyword, bool nrt) {
    if (keyword.isEmpty()) {
        return {};
    }

    log::debug() << "Search index(" << "keywork: \"" << keyword.toStdString() << "\").\n";
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

        QueryPtr query = parser_->parse(StringUtils::toUnicode(keyword.toStdString()));
        TopDocsPtr search_results = searcher->search(query, max_results);

        QStringList results;
        results.reserve(search_results->scoreDocs.size());
        for (const auto& score_doc : search_results->scoreDocs) {
            DocumentPtr doc = searcher->doc(score_doc->doc);
            QString full_path = QString::fromStdWString(doc->get(L"full_path"));
            results.append(full_path);
        }

        return results;
    } catch (const LuceneException& e) {
        throw std::runtime_error("Lucene exception: " + StringUtils::toUTF8(e.getError()));
    }
}

bool file_index_manager::document_exists(const std::string& path, bool only_check_initial_index) {
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

// TopScoreDocCollectorPtr file_index_manager::search(const std::string& path, bool exact_match, bool nrt) {
//     SearcherPtr searcher;
//     QueryPtr query;
//     TopScoreDocCollectorPtr collector = TopScoreDocCollector::create(10, true);
//     if (nrt) {
//         try_refresh_reader(true);
//         searcher = nrt_searcher_;
//     } else {
//         try_refresh_reader();
//         searcher = searcher_;
//     }

//     // Search full path for exact match, and file name for fuzzy match.
//     // Exact search only works when the field is set to Field::INDEX_NOT_ANALYZED, which avoids parsing overhead.
//     query = exact_match ? newLucene<TermQuery>(newLucene<Term>(exact_field_, StringUtils::toUnicode(path)))
//                                 : parser_->parse(StringUtils::toUnicode(path));
//     searcher->search(query, collector);
//     return collector;
// }

// Lucene::TopScoreDocCollectorPtr file_index_manager::nrt_search(const std::string& path, bool exact_match) {
//     return search(path, exact_match, true);
// }

DocumentPtr file_index_manager::create_document(const file_record& record) {
    DocumentPtr doc = newLucene<Document>();
    // File name with fuzzy match; parser is required for searching and deleting.
    doc->add(newLucene<Field>(L"file_name",
        StringUtils::toUnicode(record.file_name),
        Field::STORE_YES, Field::INDEX_ANALYZED));
    // Full path with exact match; parser is not needed for deleting and exact searching, which improves perferemce.
    doc->add(newLucene<Field>(L"full_path",
        StringUtils::toUnicode(record.full_path),
        Field::STORE_YES, Field::INDEX_NOT_ANALYZED));
    return doc;
}

ANYTHING_NAMESPACE_END