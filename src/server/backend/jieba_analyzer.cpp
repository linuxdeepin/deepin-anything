#include "jieba_analyzer.h"

#include "jieba_tokenizer.h"


ANYTHING_NAMESPACE_BEGIN

jieba_analyzer::~jieba_analyzer() {}

TokenStreamPtr jieba_analyzer::tokenStream(const String&, const ReaderPtr& reader) {
    return newLucene<jieba_tokenizer>(reader);
}

TokenStreamPtr jieba_analyzer::reusableTokenStream(const String&, const ReaderPtr& reader) {
    TokenizerPtr tokenizer(boost::dynamic_pointer_cast<Tokenizer>(getPreviousTokenStream()));
    if (!tokenizer) {
        tokenizer = newLucene<jieba_tokenizer>(reader);
        setPreviousTokenStream(tokenizer);
    } else {
        tokenizer->reset(reader);
    }
    return tokenizer;
}

ANYTHING_NAMESPACE_END