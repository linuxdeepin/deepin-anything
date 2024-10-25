#ifndef ANYTHING_JIEBA_ANALYZER_H_
#define ANYTHING_JIEBA_ANALYZER_H_

#include <lucene++/LuceneHeaders.h>

#include "anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

using namespace Lucene;

class LPPCONTRIBAPI jieba_analyzer : public Analyzer {
public:
    jieba_analyzer(std::string input);
    virtual ~jieba_analyzer() {}

    LUCENE_CLASS(jieba_analyzer);

public:
    TokenStreamPtr tokenStream(const String&, const ReaderPtr&) override;

private:
    std::string input_;
};


ANYTHING_NAMESPACE_END

#endif // ANYTHING_JIEBA_ANALYZER_H_