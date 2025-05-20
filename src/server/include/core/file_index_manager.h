// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_FILE_INDEX_MANAGER_H_
#define ANYTHING_FILE_INDEX_MANAGER_H_

#include <atomic>
#include <mutex>

#include <lucene++/LuceneHeaders.h>

#include "common/anything_fwd.hpp"
#include "core/pinyin_processor.h"

ANYTHING_NAMESPACE_BEGIN

enum class index_status {
    loading,
    scanning,
    monitoring,
    closed,
};

class file_index_manager {
public:
    explicit file_index_manager(const std::string& persistent_index_dir,
                                const std::string& volatile_index_dir,
                                const std::map<std::string, std::string>& file_type_mapping);
    ~file_index_manager();

    /// @brief Add a path to the index.
    /// @param path The full path to be added.
    void add_index(const std::string& path);

    /// @brief Remove the path from the index.
    /// @param path The full path to be removed.
    void remove_index(const std::string& path);

    /// @brief Removes the old path and inserts the new path into the index.
    /// @param old_path The existing path to be removed from the index.
    /// @param new_path The new path to be added to the index.
    void update_index(const std::string& old_path, const std::string& new_path);

    /// Commit all changes to the index
    bool commit(index_status status);

    /// @brief Persist the index to the persistent index directory
    void persist_index();

    // bool indexed() const;

    // void test(const Lucene::String& path);
    // void pinyin_test(const std::string& path);

    // /// @brief Return the size of the indexed documents.
    // /// @param nrt If true, returns the near real-time size, reflecting the most recent changes; 
    // ///            otherwise, returns the persisted size from the last commit.
    // /// @return The total number of indexed documents.
    // int32_t document_size(bool nrt = false) const;

    /// Return the cache directory of the index.
    std::string index_directory() const;

    std::vector<std::string> traverse_directory(const std::string& path, bool nrt);

    /**
     * Check if the given file path is already indexed using an exact match search.
     * @param path The file path to check.
     * @return true if the file path is already indexed, otherwise false.
     */
    bool document_exists(const std::string& path, bool only_check_initial_index = false);

    bool refresh_indexes(const std::vector<std::string>& blacklist_paths);

    void set_index_invalid();
private:
    /// Refresh the index reader if there are changes
    void try_refresh_reader(bool nrt = false);

    /**
     * Perform the actual search based on the file path.
     * @param exact_match If true, performs an exact match search. Otherwise, performs a fuzzy search 
     * (it can still perform an exact match, but requires the overhead of using the analyzer).
     * @return A collection of the search results.
     */
    // Lucene::TopScoreDocCollectorPtr search(const std::string& path, bool exact_match = false, bool nrt = false);

    /**
     * Near Real-Time Search
     * 在 NRT 模式下，即便这些数据尚未写入磁盘（commit），也可以通过获取一个近实时的 IndexReader 来从内存中读取最新的索引数据
     */
    // Lucene::TopScoreDocCollectorPtr nrt_search(const std::string& path, bool exact_match = false);

    void prepare_index();

    void check_index_version();

    void set_index_version();

    void save_index_status(index_status status);
private:
    std::string persistent_index_directory_;
    std::string volatile_index_directory_;
    Lucene::IndexWriterPtr writer_;
    Lucene::SearcherPtr searcher_;
    Lucene::SearcherPtr nrt_searcher_;
    Lucene::IndexReaderPtr reader_;
    Lucene::IndexReaderPtr nrt_reader_;
    std::mutex mtx_;
    std::mutex reader_mtx_;
    pinyin_processor pinyin_processor_;
    std::atomic<bool> search_cancelled_{false};
    const std::map<std::string, std::string> file_type_mapping_;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_FILE_INDEX_MANAGER_H_