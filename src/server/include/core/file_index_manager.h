#ifndef ANYTHING_FILE_INDEX_MANAGER_H_
#define ANYTHING_FILE_INDEX_MANAGER_H_

#include <mutex>

#include <QString>
#include <QStringList>
#include <lucene++/LuceneHeaders.h>

#include "common/anything_fwd.hpp"
#include "common/file_record.hpp"

ANYTHING_NAMESPACE_BEGIN

enum class job_status {
    pending,
    ready_for_addition,
    ready_for_deletion
};

/**
 * 开启 USE_DOUBLE_FIELD_INDEX 将启用双字段索引，能够精确删除和查找，精确查找比不开启时效率高，但占用空间更多
 * 未开启 USE_DOUBLE_FIELD_INDEX 将启用普通索引，只能模糊删除，每次查找、删除、更新时都得解析词条，影响效率
 */
class file_index_manager {
public:
    explicit file_index_manager(std::string index_dir);
    ~file_index_manager();

    /// Add a file record to the index
    void add_index(const std::string& path);

    /// 根据词条移除符合的文件索引
    /// 如果没有开启 USE_DOUBLE_FIELD_INDEX，则只能进行模糊删除，精确词条将删除失败
    /// 如果开启了 USE_DOUBLE_FIELD_INDEX，则会进行精确删除，模糊词条将删除失败
    void remove_index(const std::string& term, bool exact_match = true);

    /// Update the index for a given file record
    /// 如果 new record 索引已经存在，则会删除 old path 索引，再更新 new record（和 new record path 中的修改时间对比，更新则更新）
    /// 如果 new record 索引不存在，则会删除 old path 索引，增加 new record 索引
    void update_index(const std::string& old_path, const std::string& new_path, bool exact_match = true);

    /// 根据词条搜索文件索引
    /// @exact_match 完全匹配，开启 USE_DOUBLE_FIELD_INDEX 时能够避免解析词条带来的开销
    std::vector<file_record> search_index(const std::string& term, bool exact_match = false, bool nrt = false);

    /// Commit all changes to the index
    void commit();

    // 是否已建立过索引，document size 不为空返回 true
    bool indexed();

    void test(const Lucene::String& path);

    int document_size(bool nrt = false) const;

    std::string index_directory() const;

    QStringList search(
        const QString& path, const QString& keyword,
        int32_t offset, int32_t max_count, bool nrt);

    /**
     * Check if the given file path is already indexed using an exact match search.
     * @param path The file path to check.
     * @return true if the file path is already indexed, otherwise false.
     */
    bool document_exists(const std::string& path);

    Lucene::DocumentPtr create_document(const file_record& record);

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