#include "jieba_tokenizer.h"

#include <lucene++/ContribInc.h>
#include <lucene++/ChineseTokenizer.h>
#include <lucene++/TermAttribute.h>
#include <lucene++/OffsetAttribute.h>
#include <lucene++/Reader.h>
#include <lucene++/CharFolder.h>
#include <lucene++/MiscUtils.h>

ANYTHING_NAMESPACE_BEGIN

// const int32_t jieba_tokenizer::MAX_WORD_LEN = 255;
// const int32_t jieba_tokenizer::IO_BUFFER_SIZE = 1024;

const char* const DICT_PATH = "/home/dxnu/dxnu-github/deepin-anything/src/dict/jieba.dict.utf8";
const char* const HMM_PATH = "/home/dxnu/dxnu-github/deepin-anything/src/dict/hmm_model.utf8";
const char* const USER_DICT_PATH = "/home/dxnu/dxnu-github/deepin-anything/src/dict/user.dict.utf8";

jieba_tokenizer::jieba_tokenizer(const ReaderPtr& input)
    : jieba_tokenizer(StringUtils::toUTF8(reader_to_wstring(input))) {}

jieba_tokenizer::jieba_tokenizer(const std::string &input)
    : segment_(DICT_PATH, HMM_PATH, USER_DICT_PATH), current_index_{}, current_offset_{}, end_position_{}
{
    segment_.Cut(input, words_);
    termAtt = addAttribute<TermAttribute>();
    offsetAtt = addAttribute<OffsetAttribute>();
}

jieba_tokenizer::~jieba_tokenizer() {}

bool jieba_tokenizer::incrementToken() {
    clearAttributes(); // 清除所有词元属性

    if (current_index_ >= words_.size()) {
        return false;
    }

    const auto& word = words_[current_index_++];
    int32_t end_offset = current_offset_ + word.length();
    termAtt->setTermBuffer(StringUtils::toUnicode(word));
    offsetAtt->setOffset(correctOffset(current_offset_), correctOffset(end_offset));

    current_offset_ = end_offset;
    end_position_ = end_offset;
    return true;
}

void jieba_tokenizer::end() {
    int32_t final_offset = correctOffset(end_position_);
    offsetAtt->setOffset(final_offset, final_offset);
}

void jieba_tokenizer::reset() {
    current_index_ = 0;      // 重置索引
    current_offset_ = 0;     // 重置偏移
    end_position_ = 0;       // 重置最终位置
}

std::wstring jieba_tokenizer::reader_to_wstring(const Lucene::ReaderPtr& reader) const {
    std::wstring result;
    wchar_t buffer[1024];
    int len;

    while ((len = reader->read(buffer, 0, 1024)) != Lucene::Reader::READER_EOF) {
        result.append(buffer, len);
    }

    return result;
}

ANYTHING_NAMESPACE_END