// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "analyzers/AnythingAnalyzer.h"

#include "analyzers/AnythingFilter.h"
#include "analyzers/AnythingTokenizer.h"

ANYTHING_NAMESPACE_BEGIN

TokenStreamPtr AnythingAnalyzer::tokenStream(
    const String&, const ReaderPtr& reader) {
    TokenStreamPtr result = newLucene<AnythingTokenizer>(reader);
    result = newLucene<AnythingFilter>(result);
    return result;
}

TokenStreamPtr AnythingAnalyzer::reusableTokenStream(const String&, const ReaderPtr& reader) {
    auto streams(boost::dynamic_pointer_cast<AnythingAnalyzerSavedStreams>(getPreviousTokenStream()));
    if (!streams) {
        streams = newLucene<AnythingAnalyzerSavedStreams>();
        streams->source = newLucene<AnythingTokenizer>(reader);
        streams->result = newLucene<AnythingFilter>(streams->source);
        setPreviousTokenStream(streams);
    } else {
        streams->source->reset(reader);
    }

    return streams->result;
}

AnythingAnalyzerSavedStreams::~AnythingAnalyzerSavedStreams() {}

ANYTHING_NAMESPACE_END