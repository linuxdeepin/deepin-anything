// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "analyzers/LowerCaseNGramAnalyzer.h"

#include <lucene++/LowerCaseFilter.h>
#include <lucene++/LuceneObject.h>
#include <lucene++/NGramTokenizer.h>

ANYTHING_NAMESPACE_BEGIN

using namespace Lucene;

LowerCaseNGramAnalyzer::LowerCaseNGramAnalyzer(int32_t minGram, int32_t maxGram)
    : m_minGram(minGram),
      m_maxGram(maxGram)
{
}

LowerCaseNGramAnalyzer::~LowerCaseNGramAnalyzer() = default;

TokenStreamPtr LowerCaseNGramAnalyzer::tokenStream(const String &fieldName,
                                                    const ReaderPtr &reader)
{
    (void)fieldName;
    return newLucene<LowerCaseFilter>(
        newLucene<NGramTokenizer>(reader, m_minGram, m_maxGram));
}

TokenStreamPtr LowerCaseNGramAnalyzer::reusableTokenStream(const String &fieldName,
                                                            const ReaderPtr &reader)
{
    (void)fieldName;

    boost::shared_ptr<LowerCaseNGramTokenStreams> streams =
        boost::dynamic_pointer_cast<LowerCaseNGramTokenStreams>(getPreviousTokenStream());

    if (!streams) {
        streams = newLucene<LowerCaseNGramTokenStreams>(reader, m_minGram, m_maxGram);
        setPreviousTokenStream(streams);
    } else {
        streams->source->reset(reader);
    }

    return streams->result;
}

ANYTHING_NAMESPACE_END
