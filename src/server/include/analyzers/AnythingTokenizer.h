// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_ANYTHING_TOKENIZER_H_
#define ANYTHING_ANYTHING_TOKENIZER_H_

#include <unordered_set>

#include <lucene++/LuceneHeaders.h>

#include "common/anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

using namespace Lucene;

class AnythingTokenizer : public Tokenizer {
public:
    AnythingTokenizer(const ReaderPtr& input);

    void initialize() override;
    bool incrementToken() override;
    void end() override;
    void reset() override;
    void reset(const ReaderPtr& input) override;

private:
    void push(wchar_t c);
    bool flush();

public:
    static bool isSymbol(wchar_t c);
    static bool isDot(wchar_t c);
    static bool isLastDot(wchar_t c, int32_t offset, std::wstring buf);
    bool is_word(wchar_t* buf, int32_t len);

private:
    /// word offset, used to imply which character(in) is parsed
    int32_t offset_;

    /// the index used only for ioBuffer
    int32_t bufferIndex_;

    /// data length
    int32_t dataLen_;

    /// character buffer, store the characters which are used to compose the returned Token
    CharArray buffer_;

    /// I/O buffer, used to store the content of the input (one of the members of Tokenizer)
    CharArray ioBuffer_;

    TermAttributePtr termAtt_;
    OffsetAttributePtr offsetAtt_;

    int32_t length_;
    int32_t start_;

    std::unordered_set<std::wstring> words_;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_ANYTHING_TOKENIZER_H_