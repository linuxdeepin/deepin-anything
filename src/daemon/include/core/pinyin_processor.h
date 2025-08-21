// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_PINYIN_PROCESSOR_H_
#define ANYTHING_PINYIN_PROCESSOR_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "common/anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

class pinyin_processor {
    using dict_type = std::unordered_map<std::string, std::vector<std::string>>;
public:
    pinyin_processor() = default;
    pinyin_processor(const std::string& filename);

    void load_pinyin_dict(const std::string& filename);

    std::string convert_to_pinyin(const std::string& sentence);

private:
    unsigned int hex_to_dec(const std::string& hex_str);
    int get_utf8_char_length(unsigned char c);
    std::string remove_tone(const std::string& pinyin);

    std::vector<std::string> split_utf8_characters(const std::string& sentence);

private:
    dict_type pinyin_dict_;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_PINYIN_PROCESSOR_H_