#ifndef ANYTHING_FILE_INDEX_MANAGER_H_
#define ANYTHING_FILE_INDEX_MANAGER_H_

#include <mutex>

#include <QString>
#include <QStringList>
#include <lucene++/LuceneHeaders.h>

#include "common/anything_fwd.hpp"
#include "common/file_record.hpp"

ANYTHING_NAMESPACE_BEGIN

class file_index_manager {
public:
    explicit file_index_manager(std::string index_dir);
    ~file_index_manager();

    /// Add a path to the index.
    /// @param path The full path to be added.
    void add_index(const std::string& path);

    /// Exactly or fuzzily remove the corresponding index based on the match type.
    /// @param term The term to be removed.
    /// @param exact_match If true, performs an exact match removal; otherwise, performs a fuzzy match removal.
    void remove_index(const std::string& term, bool exact_match = true);

    /// Removes the old path and inserts the new path into the index.
    /// @param old_path The existing path to be removed from the index.
    /// @param new_path The new path to be added to the index.
    /// @param exact_match If true, performs an exact match when removing the old path; 
    ///                    otherwise, performs a fuzzy match.
    void update_index(const std::string& old_path, const std::string& new_path, bool exact_match = true);

    std::vector<file_record> search_index(const std::string& term, bool exact_match = false, bool nrt = false);

    /// Commit all changes to the index
    void commit();

    bool indexed() const;

    void test(const Lucene::String& path);

    /// Return the size of the indexed documents.
    /// @param nrt If true, returns the near real-time size, reflecting the most recent changes; 
    ///            otherwise, returns the persisted size from the last commit.
    /// @return The total number of indexed documents.
    int document_size(bool nrt = false) const;

    /// Return the cache directory of the index.
    std::string index_directory() const;

    /// Search for files in a specified directory by keyword, starting from an offset and retrieving a maximum count of results.
    /// @param path The directory path to search in (can be empty to search in all indexed directories, no performance difference).
    /// @param keyword The keyword to search for.
    /// @param offset The starting position of the search.
    /// @param max_count The maximum number of results to return.
    /// @param nrt If true, performs a near real-time search, including recent changes; 
    ///            otherwise, searches the last committed index.
    /// @return A list of file paths that match the search criteria.
    QStringList search(
        const QString& path, const QString& keyword,
        int32_t offset, int32_t max_count, bool nrt);

    /**
     * Check if the given file path is already indexed using an exact match search.
     * @param path The file path to check.
     * @return true if the file path is already indexed, otherwise false.
     */
    bool document_exists(const std::string& path, bool only_check_initial_index = false);

private:
    /// Refresh the index reader if there are changes
    void try_refresh_reader(bool nrt = false);

    /**
     * Perform the actual search based on the file path.
     * @param exact_match If true, performs an exact match search. Otherwise, performs a fuzzy search 
     * (it can still perform an exact match, but requires the overhead of using the analyzer).
     * @return A collection of the search results.
     */
    Lucene::TopScoreDocCollectorPtr search(const std::string& path, bool exact_match = false, bool nrt = false);

    /**
     * Near Real-Time Search
     * 在 NRT 模式下，即便这些数据尚未写入磁盘（commit），也可以通过获取一个近实时的 IndexReader 来从内存中读取最新的索引数据
     */
    Lucene::TopScoreDocCollectorPtr nrt_search(const std::string& path, bool exact_match = false);

    Lucene::DocumentPtr create_document(const file_record& record);

private:
    std::string index_directory_;
    Lucene::IndexWriterPtr writer_;
    Lucene::SearcherPtr searcher_;
    Lucene::SearcherPtr nrt_searcher_;
    Lucene::QueryParserPtr parser_;
    Lucene::IndexReaderPtr reader_;
    Lucene::IndexReaderPtr nrt_reader_;
    Lucene::String fuzzy_field_{ L"file_name" };
    Lucene::String exact_field_{ L"full_path" };
    std::mutex mtx_;
    std::mutex reader_mtx_;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_FILE_INDEX_MANAGER_H_