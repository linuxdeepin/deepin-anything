// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "analyzers/AnythingFilter.h"

#include <lucene++/TermAttribute.h>
#include <lucene++/UnicodeUtils.h>

#include "analyzers/AnythingTokenizer.h"

ANYTHING_NAMESPACE_BEGIN

const wchar_t* AnythingFilter::STOP_WORDS[] = {
    L"and", L"are", L"as", L"at", L"be", L"but", L"by",
    L"for", L"if", L"in", L"into", L"is", L"it",
    L"no", L"not", L"of", L"on", L"or", L"such",
    L"that", L"the", L"their", L"then", L"there", L"these",
    L"they", L"this", L"to", L"was", L"will", L"with"
};

AnythingFilter::AnythingFilter(const TokenStreamPtr& input)
    : TokenFilter(input) {
    stopTable_ = HashSet<String>::newInstance(STOP_WORDS, STOP_WORDS + SIZEOF_ARRAY(STOP_WORDS));
    termAtt_ = addAttribute<TermAttribute>();
}

bool AnythingFilter::incrementToken() {
    while (input->incrementToken()) {
        String text(termAtt_->term());

        if (!stopTable_.contains(text)) {
            if (UnicodeUtil::isLower(text[0]) || UnicodeUtil::isUpper(text[0])) {
                // English word/token should larger than 1 character.
                if (text.length() > 1) {
                    return true;
                }
            } else if (UnicodeUtil::isOther(text[0]) || UnicodeUtil::isDigit(text[0])) {
                // One Chinese character as one Chinese word.
                // Chinese word extraction to be added later here.
                return true;
            } else if (AnythingTokenizer::isDot(text[0])) {
                return true;
            }
        }
    }

    return false;
}

ANYTHING_NAMESPACE_END