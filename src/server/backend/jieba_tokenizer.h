#ifndef ANYTHING_JIEBA_TOKENIZER_H_
#define ANYTHING_JIEBA_TOKENIZER_H_

#include <cppjieba/QuerySegment.hpp>
#include <lucene++/LuceneHeaders.h>

#include "anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

using namespace Lucene;

class LPPCONTRIBAPI jieba_tokenizer : public Tokenizer {
public:
    jieba_tokenizer(const ReaderPtr& input);
    jieba_tokenizer(const std::string& input);
    virtual ~jieba_tokenizer();

    LUCENE_CLASS(jieba_tokenizer);

    virtual bool incrementToken();
    virtual void end();
    virtual void reset();

private:
    std::wstring reader_to_wstring(const Lucene::ReaderPtr& reader) const;

protected:
    TermAttributePtr termAtt;
    OffsetAttributePtr offsetAtt;

private:
    cppjieba::QuerySegment segment_;
    std::vector<std::string> words_;
    std::size_t current_index_;
    int32_t current_offset_;
    int32_t end_position_;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_JIEBA_TOKENIZER_H_