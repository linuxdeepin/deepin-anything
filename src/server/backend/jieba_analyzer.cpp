#include "jieba_analyzer.h"

#include "jieba_tokenizer.h"

ANYTHING_NAMESPACE_BEGIN

jieba_analyzer::jieba_analyzer(std::string input) : input_(std::move(input)) {}

TokenStreamPtr jieba_analyzer::tokenStream(const String&, const ReaderPtr&){
    TokenStreamPtr result = newLucene<jieba_tokenizer>(input_);
    // result = newLucene<jieba_filter>(result);
    return result;
}

ANYTHING_NAMESPACE_END