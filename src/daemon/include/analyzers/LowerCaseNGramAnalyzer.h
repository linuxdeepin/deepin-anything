// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_LOWERCASE_NGRAM_ANALYZER_H_
#define ANYTHING_LOWERCASE_NGRAM_ANALYZER_H_

#include <lucene++/LuceneHeaders.h>

#include "common/anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

using namespace Lucene;

class LowerCaseNGramAnalyzer : public Analyzer {
public:
    LowerCaseNGramAnalyzer(int32_t minGram, int32_t maxGram);
    ~LowerCaseNGramAnalyzer() override;

    TokenStreamPtr tokenStream(const String &fieldName,
                               const ReaderPtr &reader) override;
    TokenStreamPtr reusableTokenStream(const String &fieldName,
                                       const ReaderPtr &reader) override;

private:
    int32_t m_minGram;
    int32_t m_maxGram;
};

namespace {

class LowerCaseNGramTokenStreams : public LuceneObject
{
public:
    LowerCaseNGramTokenStreams(const ReaderPtr &reader, int32_t minGram, int32_t maxGram)
        : source(newLucene<NGramTokenizer>(reader, minGram, maxGram)),
          result(newLucene<LowerCaseFilter>(source))
    {
    }

    NGramTokenizerPtr source;
    TokenStreamPtr result;
};

} // namespace

ANYTHING_NAMESPACE_END

#endif // ANYTHING_LOWERCASE_NGRAM_ANALYZER_H_
