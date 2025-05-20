// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/file_index_manager.h"
#include "analyzers/AnythingAnalyzer.h"
#include "core/config.h"
#include "utils/string_helper.h"

#include <chrono>
#include <filesystem>

#include "lucene++/ChineseAnalyzer.h"
#include "lucene++/Highlighter.h"
#include "lucene++/FileUtils.h"
#include "lucene++/FuzzyQuery.h"
#include "lucene++/QueryScorer.h"
#include "lucene++/SimpleHTMLFormatter.h"

#include "analyzers/AnythingAnalyzer.h"
#include "analyzers/chineseanalyzer.h"
#include "utils/log.h"
#include "utils/tools.h"

#include <glib.h>
#include <sys/stat.h>  // For statx and struct statx
#include <fcntl.h>     // For AT_FDCWD

ANYTHING_NAMESPACE_BEGIN

using namespace Lucene;

// file_record

struct file_record {
    std::string file_name;
    std::string file_name_pinyin;
    std::string full_path;
    std::string file_type;
    std::string file_ext;
    int64_t modify_time; // milliseconds time since epoch
    int64_t file_size;
    bool is_hidden;
};

void print_file_record(const file_record& record) {
    spdlog::info("file_name: {} full_path: {} file_type: {} file_ext: {} modify_time: {} file_size: {} is_hidden: {}",
        record.file_name, record.full_path, record.file_type, record.file_ext, record.modify_time, record.file_size, record.is_hidden);
}

file_record make_file_record(const std::filesystem::path& p,
                             pinyin_processor& pinyin_processor,
                             const std::map<std::string, std::string> &file_type_mapping) {
    file_record ret = {
        .file_name       = p.filename().string(),
        .file_name_pinyin = pinyin_processor.convert_to_pinyin(p.filename().string()),
        .full_path       = p.string(),
        .file_type       = "other",
        .file_ext        = p.extension().string(),
        .modify_time     = 0,
        .file_size       = 0,
        .is_hidden       = false,
    };

    if (ret.file_ext.size() > 1) {
        ret.file_ext = ret.file_ext.substr(1);
        std::transform(ret.file_ext.begin(), ret.file_ext.end(), ret.file_ext.begin(), ::tolower);
    }
    ret.is_hidden = ret.full_path.find("/.") != std::string::npos;

    struct stat statbuf;
    if (lstat(ret.full_path.c_str(), &statbuf) != 0) {
        auto err = errno;
        spdlog::warn("stat fail: {} {}", ret.full_path, strerror(err));
        return ret;
    } else {
        ret.modify_time = statbuf.st_mtim.tv_sec;
        ret.file_size = statbuf.st_size;

        if (S_ISDIR(statbuf.st_mode)) {
            ret.file_type = "dir";
        } else if (S_ISREG(statbuf.st_mode)) {
            auto it = file_type_mapping.find(ret.file_ext);
            if (it != file_type_mapping.end()) {
                ret.file_type = it->second;
            }
        }
    }

    return ret;
}

#define FILE_NAME_FIELD L"file_name"
#define FULL_PATH_FIELD L"full_path"
#define FILE_TYPE_FIELD L"file_type"
#define FILE_EXT_FIELD L"file_ext"
#define MODIFY_TIME_FIELD L"modify_time"
#define FILE_SIZE_FIELD L"file_size"
#define FILE_SIZE_STR_FIELD L"file_size_str"
#define PINYIN_FIELD L"pinyin"
#define IS_HIDDEN_FIELD L"is_hidden"

DocumentPtr create_document(const file_record& record) {
    DocumentPtr doc = newLucene<Document>();
    // File name with fuzzy match; parser is required for searching and deleting.
    doc->add(newLucene<Field>(FILE_NAME_FIELD,
        StringUtils::toLower(StringUtils::toUnicode(record.file_name)),
        Field::STORE_YES, Field::INDEX_ANALYZED));
    // Full path with exact match; parser is not needed for deleting and exact searching, which improves perferemce.
    doc->add(newLucene<Field>(FULL_PATH_FIELD,
        StringUtils::toUnicode(record.full_path),
        Field::STORE_YES, Field::INDEX_NOT_ANALYZED));
    doc->add(newLucene<Field>(FILE_TYPE_FIELD,
        StringUtils::toUnicode(record.file_type),
        Field::STORE_YES, Field::INDEX_NOT_ANALYZED));
    doc->add(newLucene<Field>(FILE_EXT_FIELD,
        StringUtils::toUnicode(record.file_ext),
        Field::STORE_YES, Field::INDEX_NOT_ANALYZED));

    char *formatted_time = format_time(record.modify_time);
    doc->add(newLucene<Field>(MODIFY_TIME_FIELD,
        StringUtils::toUnicode(formatted_time),
        Field::STORE_YES, Field::INDEX_NOT_ANALYZED));
    g_free(formatted_time);

    char *formatted_size = format_size(record.file_size);
    doc->add(newLucene<Field>(FILE_SIZE_STR_FIELD,
        StringUtils::toUnicode(formatted_size),
        Field::STORE_YES, Field::INDEX_NOT_ANALYZED));
    g_free(formatted_size);

    doc->add(newLucene<NumericField>(MODIFY_TIME_FIELD)->setLongValue(record.modify_time));
    doc->add(newLucene<NumericField>(FILE_SIZE_FIELD)->setLongValue(record.file_size));
    doc->add(newLucene<Field>(PINYIN_FIELD,
        StringUtils::toUnicode(record.file_name_pinyin),
        Field::STORE_YES, Field::INDEX_NOT_ANALYZED));

    doc->add(newLucene<Field>(IS_HIDDEN_FIELD,
        (record.is_hidden ? L"Y" : L"N"),
        Field::STORE_YES, Field::INDEX_NOT_ANALYZED));

    return doc;
}


#define INDEX_VERSION L"1"
#define INVALID_INDEX_VERSION L"0"
#define INDEX_VERSION_FIELD L"index_version"

file_index_manager::file_index_manager(const std::string& persistent_index_dir,
                                       const std::string& volatile_index_dir,
                                       const std::map<std::string, std::string>& file_type_mapping)
    : persistent_index_directory_(persistent_index_dir),
      volatile_index_directory_(volatile_index_dir),
      pinyin_processor_("/usr/share/deepin-anything-server/pinyin.txt"),
      file_type_mapping_(file_type_mapping) {
    try {
        prepare_index();
        check_index_version();
        FSDirectoryPtr dir = FSDirectory::open(StringUtils::toUnicode(volatile_index_directory_));
        try {
            auto create = !IndexReader::indexExists(dir);
            writer_ = newLucene<IndexWriter>(dir,
                newLucene<ChineseAnalyzer>(),
                create, IndexWriter::MaxFieldLengthLIMITED);
            reader_  = IndexReader::open(dir, true);
        } catch (const LuceneException& e) {
            spdlog::warn("The index is corrupted: {}", volatile_index_directory_);
            if (writer_) writer_->close();
            if (reader_) reader_->close();
            std::filesystem::remove_all(volatile_index_directory_);

            writer_ = newLucene<IndexWriter>(dir,
                newLucene<ChineseAnalyzer>(),
                true, IndexWriter::MaxFieldLengthLIMITED);
            reader_  = IndexReader::open(dir, true);
        }
        nrt_reader_ = writer_->getReader();
        searcher_ = newLucene<IndexSearcher>(reader_);
        nrt_searcher_ = newLucene<IndexSearcher>(nrt_reader_);
    } catch (const LuceneException& e) {
        std::string error_msg = "Failed to initialize file_index_manager: " + StringUtils::toUTF8(e.getError());
        spdlog::critical(error_msg);
        throw std::runtime_error(error_msg);
    }
}

file_index_manager::~file_index_manager() {
    try {
        if (writer_) {
            commit(index_status::closed);
            writer_->close();
            persist_index();
        }

        if (searcher_) searcher_->close();
        if (nrt_searcher_) nrt_searcher_->close();
        if (reader_) reader_->close();
        if (nrt_reader_) nrt_reader_->close();
    } catch (const LuceneException& e) {
        spdlog::error("Failed to close file_index_manager: {}", StringUtils::toUTF8(e.getError()));
    }
}

void file_index_manager::add_index(const std::string& path) {
    try {
        auto doc = create_document(make_file_record(path, pinyin_processor_, file_type_mapping_));
        writer_->updateDocument(newLucene<Term>(FULL_PATH_FIELD, StringUtils::toUnicode(path)), doc);
        spdlog::debug("Indexed {}", path);
    } catch (const LuceneException& e) {
        spdlog::error("Failed to index {}: {}", path, StringUtils::toUTF8(e.getError()));
    } catch (const std::exception& e) {
        spdlog::error("Failed to index {}: {}", path, e.what());
    }
}

void file_index_manager::remove_index(const std::string& path) {
    try {
        TermPtr pterm = newLucene<Term>(FULL_PATH_FIELD, StringUtils::toUnicode(path));
        writer_->deleteDocuments(pterm);
        spdlog::debug("Removed index: {}", path);
    } catch (const LuceneException& e) {
        spdlog::error("Failed to remove index {}: {}", path, StringUtils::toUTF8(e.getError()));
    } catch (const std::exception& e) {
        spdlog::error("Failed to remove index {}: {}", path, e.what());
    }
}

void file_index_manager::update_index(const std::string& old_path, const std::string& new_path) {
    try {
        auto doc = create_document(make_file_record(new_path, pinyin_processor_, file_type_mapping_));
        writer_->updateDocument(newLucene<Term>(FULL_PATH_FIELD, StringUtils::toUnicode(old_path)), doc);
        spdlog::debug("Renamed: {} --> {}", old_path, new_path);
    } catch (const LuceneException& e) {
        spdlog::error("Failed to rename index {} to {}: {}", old_path, new_path, StringUtils::toUTF8(e.getError()));
    } catch (const std::exception& e) {
        spdlog::error("Failed to rename index {} to {}: {}", old_path, new_path, e.what());
    }
}

bool check_index_corrupted(const std::string& index_directory) {
    try {
        FSDirectoryPtr dir = FSDirectory::open(StringUtils::toUnicode(index_directory));
        Lucene::IndexReaderPtr reader = IndexReader::open(dir, true);
        reader->close();
        return false;
    } catch (const LuceneException& e) {
        spdlog::error("The index is corrupted: {}, {}", index_directory, StringUtils::toUTF8(e.getError()));
        return true;
    }
}

bool file_index_manager::commit(index_status status) {
    try {
        save_index_status(status);
        set_index_version();
        writer_->commit();
        spdlog::debug("All changes are commited with version: {}", StringUtils::toUTF8(INDEX_VERSION));
    } catch (const LuceneException& e) {
        spdlog::error("Failed to commit index: {}", StringUtils::toUTF8(e.getError()));
        return false;
    }

    return !check_index_corrupted(volatile_index_directory_);
}

void file_index_manager::persist_index() {
    std::error_code ec;

    std::filesystem::remove_all(persistent_index_directory_, ec);
    if (ec) {
        spdlog::error("Failed to remove persistent index directory: {}", ec.message());
        return;
    }

    std::filesystem::copy(volatile_index_directory_,
                          persistent_index_directory_,
                          std::filesystem::copy_options::recursive,
                          ec);
    if (ec) {
        spdlog::error("Failed to copy index to persistent index directory: {}", ec.message());
        return;
    }

    spdlog::debug("Persist index to {}", persistent_index_directory_);
}

// bool file_index_manager::indexed() const {
//     return document_size() > 0;
// }

// void file_index_manager::test(const String& path) {
//     spdlog::info("test path: {}", StringUtils::toUTF8(path));
//     // AnalyzerPtr analyzer = newLucene<StandardAnalyzer>(LuceneVersion::LUCENE_CURRENT);
//     AnalyzerPtr analyzer = newLucene<AnythingAnalyzer>();
    
//     // Use a StringReader to simulate input
//     TokenStreamPtr tokenStream = analyzer->tokenStream(L"", newLucene<StringReader>(path));
//     // TokenPtr token = newLucene<Token>();
    
//     // Tokenize and print out the results
//     while (tokenStream->incrementToken()) {
//         spdlog::info("Token: {}", StringUtils::toUTF8(tokenStream->toString()));
//     }
// }

// void file_index_manager::pinyin_test(const std::string& path) {
//     spdlog::info("pinyin test path: {}", path);
//     auto pinyin_path = pinyin_processor_.convert_to_pinyin(path);
//     test(StringUtils::toUnicode(pinyin_path));
// }

// int32_t file_index_manager::document_size(bool nrt) const {
//     return nrt ? nrt_reader_->numDocs() : reader_->numDocs();
// }

std::string file_index_manager::index_directory() const {
    return persistent_index_directory_;
}

std::vector<std::string> file_index_manager::traverse_directory(const std::string& path, bool nrt) {
    if (path.empty()) {
        return {};
    }

    std::string path_with_slash = path;
    if (!string_helper::ends_with(path_with_slash, "/")) {
        path_with_slash += "/";
    }

    String query_terms = StringUtils::toUnicode(path_with_slash);

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

        auto query = newLucene<PrefixQuery>(newLucene<Term>(FULL_PATH_FIELD, query_terms));
        auto search_results = searcher->search(query, max_results);

        std::vector<std::string> results;
        results.reserve(search_results->scoreDocs.size());
        for (const auto& score_doc : search_results->scoreDocs) {
            DocumentPtr doc = searcher->doc(score_doc->doc);
            results.push_back(StringUtils::toUTF8(doc->get(FULL_PATH_FIELD)));
        }

        return results;
    } catch (const LuceneException& e) {
        spdlog::error("Failed to traverse directory {}: {}", path, StringUtils::toUTF8(e.getError()));
        return {};
    }
}

bool file_index_manager::document_exists(const std::string &path, bool only_check_initial_index) {
    TermPtr term = newLucene<Term>(FULL_PATH_FIELD, StringUtils::toUnicode(path));
    TermDocsPtr termDocs;
    if (only_check_initial_index) {
        termDocs = reader_->termDocs(term);
    } else {
        try_refresh_reader(true);
        termDocs = nrt_reader_->termDocs(term);
    }
    return termDocs->next();
}

bool file_index_manager::refresh_indexes(const std::vector<std::string>& blacklist_paths) {
    bool index_changed = false;
    try {
        std::error_code ec;
        spdlog::info("Refreshing file indexes...");
        try_refresh_reader();
        auto query = newLucene<Lucene::MatchAllDocsQuery>();
        auto num_docs = reader_->numDocs();
        if (num_docs > 0) {
            auto search_results = searcher_->search(query, num_docs);
            std::vector<std::string> remove_list;
            for (const auto& score_doc : search_results->scoreDocs) {
                DocumentPtr doc = searcher_->doc(score_doc->doc);
                std::filesystem::path full_path(doc->get(FULL_PATH_FIELD));
                if (full_path.empty()) {
                    // doc is metadata, not a file
                    continue;
                }
                if (!std::filesystem::exists(full_path, ec) ||
                    is_path_in_blacklist(full_path.string(), blacklist_paths)) {
                    remove_list.push_back(full_path.string());
                }
            }

            for (const auto& path : remove_list) {
                // Remove non-existent path from the indexes
                remove_index(path);
                index_changed = true;
            }
        }
    } catch (const LuceneException& e) {
        spdlog::error("Failed to refresh indexes: {}", StringUtils::toUTF8(e.getError()));
    } catch (const std::exception& e) {
        spdlog::error("Failed to refresh indexes: {}", e.what());
    }

    return index_changed;
}

void file_index_manager::set_index_invalid()
{
    try {
        set_index_version();

        DocumentPtr doc = newLucene<Document>();
        doc->add(newLucene<Field>(INDEX_VERSION_FIELD, INVALID_INDEX_VERSION, Field::STORE_YES, Field::INDEX_NOT_ANALYZED));
        writer_->updateDocument(newLucene<Term>(INDEX_VERSION_FIELD, INDEX_VERSION), doc);
    } catch (const LuceneException& e) {
        spdlog::error("Failed to set index invalid: {}", StringUtils::toUTF8(e.getError()));
    } catch (const std::exception& e) {
        spdlog::error("Failed to set index invalid: {}", e.what());
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

void file_index_manager::prepare_index() {
    spdlog::info("Preparing index...");
    std::error_code ec;
    if (!std::filesystem::exists(volatile_index_directory_, ec)) {
        if (ec) {
            spdlog::error("Failed to check volatile index directory: {}", ec.message());
            exit(EXIT_FAILURE);
        }

        if (!std::filesystem::exists(persistent_index_directory_, ec)) {
            spdlog::info("Persistent index directory does not exist: {}", persistent_index_directory_);
            return;
        }

        ec.clear();
        std::filesystem::copy(persistent_index_directory_,
                              volatile_index_directory_,
                              std::filesystem::copy_options::recursive,
                              ec);
        if (ec) {
            spdlog::error("Failed to copy index to volatile index directory: {}", ec.message());
        } else {
            spdlog::info("Prepared index in {}", volatile_index_directory_);
        }
    } else {
        spdlog::info("Index already exists in {}", volatile_index_directory_);
    }
}

void file_index_manager::check_index_version() {
    spdlog::info("Checking index version...");
    Lucene::IndexReaderPtr reader;

    std::error_code ec;
    if (!std::filesystem::exists(volatile_index_directory_, ec)) {
        spdlog::info("Index directory does not exist: {}", volatile_index_directory_);
        return;
    }

    // 打开索引
    try {
        FSDirectoryPtr dir = FSDirectory::open(StringUtils::toUnicode(volatile_index_directory_));
        reader = IndexReader::open(dir, true);
    } catch (const LuceneException& e) {
        spdlog::warn("The index is corrupted: {}", volatile_index_directory_);
        if (reader) reader->close();
        spdlog::warn("Removing the corrupted index...");
        std::filesystem::remove_all(volatile_index_directory_);
        return;
    }

    // 查找数据库版本
    bool found = false;
    try {
        TermPtr term = newLucene<Term>(INDEX_VERSION_FIELD, INDEX_VERSION);
        QueryPtr query = newLucene<TermQuery>(term);
        Lucene::SearcherPtr searcher = newLucene<IndexSearcher>(reader);
        Lucene::TopDocsPtr top_docs = searcher->search(query, 1);
        found = top_docs->scoreDocs.size() == 1;
        reader->close();
    } catch (const LuceneException& e) {
        spdlog::error("Failed to check index version: {}", StringUtils::toUTF8(e.getError()));
    } catch (const std::exception& e) {
        spdlog::error("Failed to check index version: {}", e.what());
    }
    if (found) {
        spdlog::info("The index version is expected({}): {}", StringUtils::toUTF8(INDEX_VERSION), volatile_index_directory_);
    } else {
        spdlog::warn("The index version is mismatched: {}", volatile_index_directory_);
        spdlog::warn("Removing the incompatible index...");
        std::filesystem::remove_all(volatile_index_directory_);
    }
}

void file_index_manager::set_index_version() {
    static std::once_flag flag;
    std::call_once(flag, [this]() {
        // 保存版本号到数据库
        try {
            DocumentPtr doc = newLucene<Document>();
            doc->add(newLucene<Field>(INDEX_VERSION_FIELD, INDEX_VERSION, Field::STORE_YES, Field::INDEX_NOT_ANALYZED));
            writer_->updateDocument(newLucene<Term>(INDEX_VERSION_FIELD, INDEX_VERSION), doc);
        } catch (const LuceneException& e) {
            spdlog::error("Failed to set index version: {}", StringUtils::toUTF8(e.getError()));
        } catch (const std::exception& e) {
            spdlog::error("Failed to set index version: {}", e.what());
        }
    });
}

static const char * const status_json_template = R"({
    "time": "%s",
    "status": "%s",
    "version": "%s"
}
)";

void file_index_manager::save_index_status(index_status status) {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_time_t), "%Y-%m-%dT%H:%M:%S");

    std::string status_str;
    switch (status) {
        case index_status::loading:
            status_str = "loading";
            break;
        case index_status::scanning:
            status_str = "scanning";
            break;
        case index_status::monitoring:
            status_str = "monitoring";
            break;
        case index_status::closed:
            status_str = "closed";
            break;
        default:
            assert(false && "Invalid index status");
            break;
    }

    char status_json[1024];
    int written = snprintf(status_json, sizeof(status_json), status_json_template,
             ss.str().c_str(),
             status_str.c_str(),
             StringUtils::toUTF8(INDEX_VERSION).c_str());
    if (written < 0 || written >= (int)sizeof(status_json)) {
        spdlog::error("Failed to format status json");
        return;
    }

    try {
        std::ofstream status_file(volatile_index_directory_ + "/status.json");
        status_file << status_json;
        status_file.close();
    } catch (const std::exception& e) {
        spdlog::error("Failed to save index status: {}", e.what());
    }
}

ANYTHING_NAMESPACE_END
