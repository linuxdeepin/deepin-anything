#include "core/file_index_manager.h"

#include <filesystem>
#include <iostream>

#include "lucene++/FileUtils.h"
#include "lucene++/ChineseAnalyzer.h"

// #include "jieba_analyzer.h"
#include "utils/log.h"
#include "utils/string_helper.h"

ANYTHING_NAMESPACE_BEGIN

using namespace Lucene;

file_index_manager::file_index_manager(std::string index_dir, std::size_t batch_size)
    : index_directory_{ std::move(index_dir) },
      batch_size_{ batch_size } {
    try {
        FSDirectoryPtr dir = FSDirectory::open(StringUtils::toUnicode(index_directory_));
        auto create = !IndexReader::indexExists(dir);
        writer_ = newLucene<IndexWriter>(dir,
            // newLucene<jieba_analyzer>(),
            newLucene<ChineseAnalyzer>(),
            create, IndexWriter::MaxFieldLengthLIMITED);
        reader_  = IndexReader::open(dir, true);
        nrt_reader_ = writer_->getReader();
        searcher_ = newLucene<IndexSearcher>(reader_);
        nrt_searcher_ = newLucene<IndexSearcher>(nrt_reader_);
        parser_ = newLucene<QueryParser>(
            LuceneVersion::LUCENE_CURRENT, fuzzy_field_,
            newLucene<ChineseAnalyzer>()); // newLucene<jieba_analyzer>());
    } catch (const LuceneException& e) {
        throw std::runtime_error("Lucene exception: " + StringUtils::toUTF8(e.getError()));
    }
}

file_index_manager::~file_index_manager() {
    // log::debug("{}", __PRETTY_FUNCTION__);
    if (writer_) {
        if (!addition_batch_.empty()) {
            process_addition_jobs();
        }
        if (!deletion_batch_.empty()) {
            process_deletion_jobs();
        }
        this->commit();
        writer_->close();
    }

    if (searcher_) searcher_->close();
    if (nrt_searcher_) nrt_searcher_->close();
    if (reader_) reader_->close();
    if (nrt_reader_) nrt_reader_->close();
}

void file_index_manager::add_index(file_record record) {
    // std::this_thread::sleep_for(std::chrono::milliseconds(10));
    log::debug("Indexed {}", record.full_path);

    // try {
    //     if (!document_exists(record.full_path)) {
    //         // auto doc = create_document(record);
    //         // (void)(doc);
    //         std::this_thread::sleep_for(std::chrono::milliseconds(10));
    //         log::debug("Indexed {}", record.full_path);
    //         // writer_->addDocument(create_document(record));
    //     } else {
    //         log::debug("Already indexed {}", record.full_path);
    //     }
    // } catch (const LuceneException& e) {
    //     log::error("Failed to index {}: {}", record.full_path, StringUtils::toUTF8(e.getError()));
    //     throw std::runtime_error("Lucene exception: " + StringUtils::toUTF8(e.getError()));
    // }
}

void file_index_manager::add_index(const std::filesystem::path& full_path) {
    try {
        if (!document_exists(full_path)) {
            auto doc = create_document(file_helper::make_file_record(full_path));
            writer_->addDocument(doc);
        }
    } catch (const LuceneException& e) {
        log::error("Failed to index {}: {}", full_path.string(), StringUtils::toUTF8(e.getError()));
        throw std::runtime_error("Lucene exception: " + StringUtils::toUTF8(e.getError()));
    }
}

void file_index_manager::add_index_delay(file_record record) {
    // log::debug("{}", __PRETTY_FUNCTION__);
    if (should_be_filtered(record)) {
        return;
    }

    // std::lock_guard<std::mutex> lock(mtx_);
    if (!document_exists(record.full_path)) {
        addition_batch_.push_back(create_document(record));
    } else {
        // log::info("Already indexed {}", record.full_path);
    }
}

void file_index_manager::remove_index(const std::string& term, bool exact_match) {
    try {
        if (exact_match) {
            // 精确删除，term 须是路径
            // TermPtr 是精确删除，field 如果使用了分词，便无法找到，导致删除失败，因此这种方式所对应的 term 必须是 Non-tokenized
            TermPtr pterm = newLucene<Term>(exact_field_, StringUtils::toUnicode(term));
            writer_->deleteDocuments(pterm);
        } else {
            // 模糊删除，term 可以是文件名
            // 如果 field 已经是分词，则使用 parser 解析后再删除
            QueryPtr query = parser_->parse(StringUtils::toUnicode(term));
            writer_->deleteDocuments(query);
        }
        // log::debug("Removed index: {}", term);
    } catch (const LuceneException& e) {
        throw std::runtime_error("Lucene exception: " + StringUtils::toUTF8(e.getError()));
    }
}

void file_index_manager::remove_index_delay(std::string term, bool exact_match) {
    if (should_be_filtered(term)) {
        return;
    }

    deletion_batch_.emplace_back(std::move(term), exact_match);
}

std::vector<file_record> file_index_manager::search_index(const std::string& term, bool exact_match, bool nrt) {
    std::vector<file_record> results;
    TopScoreDocCollectorPtr collector = search(term, exact_match, nrt);

    if (collector->getTotalHits() == 0) {
        std::wcout << L"Found no files\n";
        return results;
    }

    SearcherPtr searcher = nrt ? nrt_searcher_ : searcher_;
    Collection<ScoreDocPtr> hits = collector->topDocs()->scoreDocs;
    for (int32_t i = 0; i < hits.size(); ++i) {
        DocumentPtr doc = searcher->doc(hits[i]->doc);
        file_record record;
        String file_name = doc->get(L"file_name");
        String full_path = doc->get(L"full_path");
        String last_write_time = doc->get(L"last_write_time");
        if (!file_name.empty()) record.file_name = StringUtils::toUTF8(file_name);
        if (!full_path.empty()) record.full_path = StringUtils::toUTF8(full_path);
        // std::cout << "------------------\n";
        // std::cout << "full_path: " << record.full_path << "\n";
        // std::cout << "modified: " << record.modified << "\n";
        // std::cout << "------------------\n";
        results.push_back(std::move(record));
    }

    return results;
}

void file_index_manager::update_index(const std::string& old_path, file_record record) {
    // log::debug("{}", __PRETTY_FUNCTION__);
    if (should_be_filtered(old_path)) {
        return;
    }

    update(old_path, std::move(record));
}

void file_index_manager::commit() {
    writer_->commit();
    log::info("All changes are commited");
}

bool file_index_manager::indexed() {
    return document_size() > 0;
}

void file_index_manager::test(const String& path) {
    log::info("test path: {}", StringUtils::toUTF8(path));
    AnalyzerPtr analyzer = newLucene<ChineseAnalyzer>(); // newLucene<jieba_analyzer>();
    
    // Use a StringReader to simulate input
    TokenStreamPtr tokenStream = analyzer->tokenStream(L"", newLucene<StringReader>(path));
    TokenPtr token = newLucene<Token>();
    
    // Tokenize and print out the results
    while (tokenStream->incrementToken()) {
        log::info("Token: {}", StringUtils::toUTF8(tokenStream->toString()));
    }
}

int file_index_manager::document_size(bool nrt) const {
    return nrt ? nrt_reader_->numDocs() : reader_->numDocs();
}

std::string file_index_manager::index_directory() const {
    return index_directory_;
}

void file_index_manager::process_addition_jobs() {
    for (const auto& doc : addition_batch_) {
        writer_->addDocument(doc);
        QString full_path = QString::fromStdWString(doc->get(L"full_path"));
        log::debug("Indexed: {}", full_path.toStdString());
    }

    addition_batch_.clear();
    last_addition_time_ = std::chrono::steady_clock::now();
}

void file_index_manager::process_deletion_jobs() {
    for (const auto& [term, exact_match] : deletion_batch_) {
        try {
            if (exact_match) {
                TermPtr pterm = newLucene<Term>(exact_field_, StringUtils::toUnicode(term));
                writer_->deleteDocuments(pterm);
            } else {
                QueryPtr query = parser_->parse(StringUtils::toUnicode(term));
                writer_->deleteDocuments(query);
            }
            log::debug("Removed index: {}", term);
        } catch (const LuceneException& e) {
            throw std::runtime_error("Lucene exception: " + StringUtils::toUTF8(e.getError()));
        }
    }

    deletion_batch_.clear();
    last_deletion_time_ = std::chrono::steady_clock::now();
}

bool file_index_manager::addition_jobs_ready() const {
    return addition_batch_.size() >= batch_size_ ||
        (std::chrono::steady_clock::now() - last_addition_time_) >= batch_interval_;
}

bool file_index_manager::deletion_jobs_ready() const {
    return deletion_batch_.size() > batch_size_ ||
        (std::chrono::steady_clock::now() - last_deletion_time_) >= batch_interval_;
}

QStringList file_index_manager::search(
    const QString& path, const QString& keyword,
    int32_t offset, int32_t max_count, bool nrt) {
    log::debug("Search index(path:\"{}\", keywork: \"{}\").", path.toStdString(), keyword.toStdString());
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
            log::debug("No more results(path:\"{}\", keywork: \"{}\").", path.toStdString(), keyword.toStdString());
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

bool file_index_manager::document_exists(const std::string& path) {
    auto collector = nrt_search(path, true);
    return collector->getTotalHits() > 0;
}

void file_index_manager::set_index_change_filter(
    std::function<bool(const std::string&)> filter) {
    index_change_filter_ = std::move(filter);
}

void file_index_manager::try_refresh_reader(bool nrt) {
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

TopScoreDocCollectorPtr file_index_manager::search(const std::string& path, bool exact_match, bool nrt) {
    SearcherPtr searcher;
    if (nrt) {
        try_refresh_reader(true);
        searcher = nrt_searcher_;
    } else {
        try_refresh_reader();
        searcher = searcher_;
    }

    // 精确搜索时搜路径，模糊搜索时搜文件名
    // 精确搜索只有在 field 为 Field::INDEX_NOT_ANALYZED 才有效，可以避免解析开销
    QueryPtr query = exact_match ? newLucene<TermQuery>(newLucene<Term>(exact_field_, StringUtils::toUnicode(path)))
                                 : parser_->parse(StringUtils::toUnicode(path));
    TopScoreDocCollectorPtr collector = TopScoreDocCollector::create(10, true);
    searcher->search(query, collector);
    return collector;
}

Lucene::TopScoreDocCollectorPtr file_index_manager::nrt_search(const std::string& path, bool exact_match) {
    return search(path, exact_match, true);
}

void file_index_manager::update(const std::string& term, file_record record, bool exact_match) {
    log::debug("Renamed: {} --> {}", term, record.full_path);
    if (exact_match) {
        // 精确更新，term 必须是路径，否则 updateDocument 无法删除
        auto doc = create_document(record);
        writer_->updateDocument(newLucene<Term>(L"full_path", StringUtils::toUnicode(term)), doc);
    } else {
        // 模糊更新，term 可以是文件名，此时手动调用 remove_index 和 insert_index 来更新
        this->remove_index(term);
        this->add_index(std::move(record));
    }
}

DocumentPtr file_index_manager::create_document(const file_record& record) {
    DocumentPtr doc = newLucene<Document>();
    // 文件名，模糊匹配，查询和删除时必须使用解析器
    doc->add(newLucene<Field>(L"file_name",
        StringUtils::toUnicode(record.file_name),
        Field::STORE_YES, Field::INDEX_ANALYZED));
    // 路径名，精确匹配，删除和精确搜索时无需使用解析器，效率更高
    doc->add(newLucene<Field>(L"full_path",
        StringUtils::toUnicode(record.full_path),
        Field::STORE_YES, Field::INDEX_NOT_ANALYZED));
    return doc;
}

bool file_index_manager::should_be_filtered(const file_record& record) const {
    return should_be_filtered(record.full_path);
}

bool file_index_manager::should_be_filtered(const std::string& path) const {
    if (index_change_filter_) {
        return std::invoke(index_change_filter_, path);
    }

    return false;
}

ANYTHING_NAMESPACE_END