// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_ANYTHING_ANALYZER_H_
#define ANYTHING_ANYTHING_ANALYZER_H_

#include <lucene++/LuceneHeaders.h>
#include <lucene++/TermAttribute.h>

#include "common/anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

using namespace Lucene;

class AnythingAnalyzer : public Analyzer {
public:
    TokenStreamPtr tokenStream(const String&, const ReaderPtr& reader) override;

    TokenStreamPtr reusableTokenStream(const String&, const ReaderPtr& reader) override;

    template<typename Func>
    static void forEachTerm(const String& str, Func callback) {
        AnalyzerPtr analyzer = newLucene<AnythingAnalyzer>();
        TokenStreamPtr tokenStream = analyzer->tokenStream(L"", newLucene<StringReader>(str));
        while (tokenStream->incrementToken()) {
            callback(tokenStream->getAttribute<TermAttribute>()->term());
        }
    };
};

class AnythingAnalyzerSavedStreams : public LuceneObject {
public:
    virtual ~AnythingAnalyzerSavedStreams();

    LUCENE_CLASS(AnythingAnalyzerSavedStreams);

public:
    TokenizerPtr source;
    TokenStreamPtr result;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_ANYTHING_ANALYZER_H_