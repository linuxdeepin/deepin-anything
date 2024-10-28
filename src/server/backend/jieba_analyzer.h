#ifndef ANYTHING_JIEBA_ANALYZER_H_
#define ANYTHING_JIEBA_ANALYZER_H_

#include <lucene++/LuceneHeaders.h>

#include "anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

using namespace Lucene;

class LPPCONTRIBAPI jieba_analyzer : public Analyzer {
public:
    virtual ~jieba_analyzer();

    LUCENE_CLASS(jieba_analyzer);

    TokenStreamPtr tokenStream(const String& fieldName, const ReaderPtr& reader) override;
    TokenStreamPtr reusableTokenStream(const String& fieldName, const ReaderPtr& reader) override;
};


ANYTHING_NAMESPACE_END

#endif // ANYTHING_JIEBA_ANALYZER_H_