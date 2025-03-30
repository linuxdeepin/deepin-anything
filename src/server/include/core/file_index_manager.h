// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_FILE_INDEX_MANAGER_H_
#define ANYTHING_FILE_INDEX_MANAGER_H_

#include <atomic>
#include <mutex>

#include <QString>
#include <QStringList>
#include <lucene++/LuceneHeaders.h>

#include "common/anything_fwd.hpp"
#include "common/file_record.h"
#include "core/pinyin_processor.h"

ANYTHING_NAMESPACE_BEGIN

class file_index_manager {
public:
    explicit file_index_manager(std::string persistent_index_dir, std::string volatile_index_dir);
    ~file_index_manager();

    /// @brief Add a path to the index.
    /// @param path The full path to be added.
    void add_index(const std::string& path);

    /// @brief Exactly or fuzzily remove the corresponding index based on the match type.
    /// @param term The term to be removed.
    /// @param exact_match If true, performs an exact match removal; otherwise, performs a fuzzy match removal.
    void remove_index(const std::string& term, bool exact_match = true);

    /// @brief Removes the old path and inserts the new path into the index.
    /// @param old_path The existing path to be removed from the index.
    /// @param new_path The new path to be added to the index.
    /// @param exact_match If true, performs an exact match when removing the old path; 
    ///                    otherwise, performs a fuzzy match.
    void update_index(const std::string& old_path, const std::string& new_path, bool exact_match = true);

    // std::vector<file_record> search_index(const std::string& term, bool exact_match = false, bool nrt = false);

    /// Commit all changes to the index
    void commit();

    /// @brief Persist the index to the persistent index directory
    void persist_index();

    bool indexed() const;

    void test(const Lucene::String& path);
    void pinyin_test(const std::string& path);

    /// @brief Return the size of the indexed documents.
    /// @param nrt If true, returns the near real-time size, reflecting the most recent changes; 
    ///            otherwise, returns the persisted size from the last commit.
    /// @return The total number of indexed documents.
    int32_t document_size(bool nrt = false) const;

    /// Return the cache directory of the index.
    std::string index_directory() const;

    /// @brief Search for files in a specified directory by keyword, starting from an offset and retrieving a maximum count of results.
    /// @param path The directory path to search in (can be empty to search in all indexed directories, no performance difference).
    /// @param keywords The keywords to search for.
    /// @param offset The starting position of the search.
    /// @param max_count The maximum number of results to return.
    /// @param nrt If true, performs a near real-time search, including recent changes; 
    ///            otherwise, searches the last committed index.
    /// @param highlight Specifies if the search results should contain keywords highlighted for emphasis.
    /// @return A list of file paths that match the search criteria.
    QStringList search(const QString& path, QString& keywords,
        int32_t offset, int32_t max_count, bool nrt);
    
    /// @brief Searches all files for the specified keywords and returns a list of matching file names.
    /// @param keywords The keywords to search for.
    /// @param nrt If true, performs a near real-time search, including recent changes; 
    ///            otherwise, searches the last committed index.
    /// @return A QStringList containing the paths of all files where the keywords are found.
    ///         If no files are found, an empty list is returned.
    QStringList search(const QString& path, QString& keywords, bool nrt);

    /// @brief Searches all files for the specified keywords and file types, returning a list of matching file names.
    /// @param keywords The keywords to search for.
    /// @param type The type of files to search in (e.g., "txt", "cpp"). If empty, all file types are included in the search.
    /// @param nrt If true, performs a near real-time search that includes recent changes not yet committed; 
    ///            otherwise, searches the last committed index for more stable results.
    /// @return A QStringList containing the paths of all files where the keywords are found. 
    ///         If no files are found, an empty list is returned.
    QStringList search(const QString& path, QString& keywords, const QString& type, bool nrt);

    /// @brief Searches all files created between a specified time range, returning a list of matching file names.
    /// @param keywords The keywords to search for.
    /// @param after The start time for the search range. Files created after this time will be included in the search.
    ///              The time should be provided in a recognized format (e.g., "YYYY-MM-DD HH:MM:SS"). If this parameter is empty,
    ///              the search will include all files created before the `before` parameter.
    /// @param before The end time for the search range. Files created before this time will be included in the search.
    ///               The time should be provided in a recognized format (e.g., "YYYY-MM-DD HH:MM:SS"). If this parameter is empty,
    ///               the search will include all files created after the `after` parameter.
    ///               If both `after` and `before` are empty, the search will include all files matching the keywords regardless of time.
    /// @param nrt If true, performs a near real-time search that includes recent changes not yet committed; 
    ///            otherwise, searches the last committed index for more stable results.
    /// @return A QStringList containing the paths of all files where the keywords are found within the specified time range. 
    ///         If no files are found, an empty list is returned.
    QStringList search(const QString& path, QString& keywords, const QString& after, const QString& before, bool nrt);

    void async_search(QString& keywords, bool nrt, std::function<void(const QStringList&)> callback);

    QStringList traverse_directory(const QString& path, bool nrt);

    /**
     * Check if the given file path is already indexed using an exact match search.
     * @param path The file path to check.
     * @return true if the file path is already indexed, otherwise false.
     */
    bool document_exists(const std::string& path, bool only_check_initial_index = false);

    bool refresh_indexes();

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

    Lucene::DocumentPtr create_document(const file_record& record);

    void prepare_index();
private:
    std::string persistent_index_directory_;
    std::string volatile_index_directory_;
    Lucene::IndexWriterPtr writer_;
    Lucene::SearcherPtr searcher_;
    Lucene::SearcherPtr nrt_searcher_;
    Lucene::QueryParserPtr parser_;
    Lucene::QueryParserPtr type_parser_;
    Lucene::QueryParserPtr pinyin_parser_;
    Lucene::IndexReaderPtr reader_;
    Lucene::IndexReaderPtr nrt_reader_;
    Lucene::String fuzzy_field_{ L"file_name" };
    Lucene::String exact_field_{ L"full_path" };
    Lucene::String type_field_{ L"file_type" };
    Lucene::String pinyin_field_{ L"pinyin" };
    std::mutex mtx_;
    std::mutex reader_mtx_;
    file_helper file_helper_;
    pinyin_processor pinyin_processor_;
    std::atomic<bool> search_cancelled_{false};
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_FILE_INDEX_MANAGER_H_