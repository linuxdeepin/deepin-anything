// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_ANYTHING_FILTER_H_
#define ANYTHING_ANYTHING_FILTER_H_

#include <lucene++/LuceneHeaders.h>

#include "common/anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

using namespace Lucene;

class AnythingFilter : public TokenFilter {
public:
    AnythingFilter(const TokenStreamPtr& input);

    bool incrementToken() override;

private:
    HashSet<String> stopTable_;
    TermAttributePtr termAtt_;

    static const wchar_t* STOP_WORDS[];
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_ANYTHING_FILTER_H_