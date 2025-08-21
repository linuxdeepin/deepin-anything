// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "analyzers/AnythingTokenizer.h"

#include <lucene++/CharFolder.h>
#include <lucene++/OffsetAttribute.h>
#include <lucene++/TermAttribute.h>
#include <lucene++/UnicodeUtils.h>

#include "utils/log.h"

#include <iostream>

ANYTHING_NAMESPACE_BEGIN

constexpr int32_t max_word_len = 512;
constexpr int32_t io_buffer_size = 1024;

AnythingTokenizer::AnythingTokenizer(const ReaderPtr& input)
    : Tokenizer(input) {
    // load_dictionary("config/dict.utf8",
    //     [this](std::wstring&& word) {
    //     words_.insert(word);
    // });
}

void AnythingTokenizer::initialize() {
    offset_ = 0;
    bufferIndex_ = 0;
    dataLen_ = 0;
    buffer_ = CharArray::newInstance(max_word_len);
    ioBuffer_ = CharArray::newInstance(io_buffer_size);
    length_ = 0;
    start_ = 0;

    termAtt_ = addAttribute<TermAttribute>();
    offsetAtt_ = addAttribute<OffsetAttribute>();
}

bool AnythingTokenizer::incrementToken() {
    clearAttributes();

    length_ = 0;
    start_ = offset_;
    bool last_is_en  = false;
    bool last_is_num = false;
    bool last_is_sym = false;

    while (true) {
        wchar_t c;
        ++offset_;

        if (bufferIndex_ >= dataLen_) {
            std::memset(ioBuffer_.get(), 0, ioBuffer_.size() * sizeof(wchar_t));
            dataLen_ = input->read(ioBuffer_.get(), 0, ioBuffer_.size());
            bufferIndex_ = 0;

            // if (dataLen_ != -1) {
            //     std::wcout << L"Buffer: " << ioBuffer_.get() << " dataLen: " << dataLen_
            //         << " ioBuffer size: " << ioBuffer_.size() << L"\n";
            // }
        }

        if (dataLen_ == -1) {
            --offset_;
            return flush();
        } else {
            c = ioBuffer_[bufferIndex_++];
        }

        if (UnicodeUtil::isLower(c) || UnicodeUtil::isUpper(c)) { // 字母
            if (last_is_num || last_is_sym) {
                --bufferIndex_;
                --offset_;
                return flush();
            }

            push(c);
            if (length_ == max_word_len) {
                return flush();
            }
            // Read to the end
            if (bufferIndex_ >= dataLen_ - 1 && length_ == 1) {
                if (c == L'c' || c == L'r' || c == L'a') {
                    return flush();
                }
            }
            last_is_en = true;
        } else if (UnicodeUtil::isDigit(c)) { // 数字
            if (last_is_en || last_is_sym) {
                --bufferIndex_;
                --offset_;
                return flush();
            }

            push(c);
            if (length_ == max_word_len) {
                return flush();
            }
            last_is_num = true;
        } else if (isDot(c)) {
            // Detect the version number, for example: v1.0.0
            if (last_is_num && bufferIndex_ < dataLen_ &&
                UnicodeUtil::isDigit(ioBuffer_[bufferIndex_])) {
                push(c);
            } else {
                if (length_ > 0) {
                    return flush();
                }
            }
        } else if (isSymbol(c)) {
            // Detect C++ or C#, etc...
            if (last_is_en || last_is_sym) {
                push(c);
                last_is_sym = true;
            } else {
                if (length_ > 0) {
                    return flush();
                }
            }
        } else if (isPreservedSymbol(c)) {
            if (length_ > 0) {
                --bufferIndex_;
                --offset_;
                return flush();
            }
            push(c);
            // return flush();
        } else if (UnicodeUtil::isOther(c)) {
            if (length_ > 0) {
                --bufferIndex_;
                --offset_;
                return flush();
            }
            push(c);
            return flush();
        } else if (length_ > 0) {
            return flush();
        }
    }
}

void AnythingTokenizer::end() {
    // set final offset
    int32_t finalOffset = correctOffset(offset_);
    offsetAtt_->setOffset(finalOffset, finalOffset);
}

void AnythingTokenizer::reset() {
    // std::cout << __PRETTY_FUNCTION__ << "\n";
    Tokenizer::reset();
    // input->reset();
    offset_ = 0;
    bufferIndex_ = 0;
    dataLen_ = 0;
}

void AnythingTokenizer::reset(const ReaderPtr& input) {
    Tokenizer::reset(input);
    reset();
}

void AnythingTokenizer::push(wchar_t c) {
    if (length_ == 0) {
        start_ = offset_ - 1; // start of token
    }

    buffer_[length_++] = CharFolder::toLower(c);
}

// 处理当前积累的 token
bool AnythingTokenizer::flush() {
    if (length_ > 0) {
        termAtt_->setTermBuffer(buffer_.get(), 0, length_);
        offsetAtt_->setOffset(correctOffset(start_), correctOffset(start_ + length_));
        std::memset(buffer_.get(), 0, buffer_.size() * sizeof(wchar_t));
        return true;
    }

    return false;
}

bool AnythingTokenizer::isSymbol(wchar_t c) {
    return c == L'+' || c == L'#';
}

bool AnythingTokenizer::isPreservedSymbol(wchar_t c) {
    return c == L'@';
}

bool AnythingTokenizer::isDot(wchar_t c) {
    return c == L'.';
}

bool AnythingTokenizer::isLastDot(wchar_t c, int32_t offset, std::wstring buf) {
    if (isDot(c)) {
        auto lastDotPosition = buf.find_last_of(L'.');
        if (lastDotPosition != buf.size() - 1) {
            return static_cast<size_t>(offset) == lastDotPosition;
        }
    }

    return false;
}

bool AnythingTokenizer::is_word(wchar_t* buf, int32_t len) {
    if (auto it = words_.find(std::wstring(buf, len)); it != words_.end()) {
        return true;
    }

    return false;
}

ANYTHING_NAMESPACE_END
