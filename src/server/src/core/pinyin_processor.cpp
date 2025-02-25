// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/pinyin_processor.h"

#include <codecvt>
#include <fstream>
#include <locale>

#include "utils/string_helper.h"

ANYTHING_NAMESPACE_BEGIN

pinyin_processor::pinyin_processor(const std::string& filename) {
    load_pinyin_dict(filename);
}

void pinyin_processor::load_pinyin_dict(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && string_helper::starts_with(line, "U+")) {
            if (auto colon_pos = line.find_first_of(':');
                colon_pos != std::string::npos) {
                auto unicode = line.substr(2, colon_pos - 2); // index, length

                // Convert hex string to decimal and then to wchar_t
                wchar_t unicode_char = static_cast<wchar_t>(hex_to_dec(unicode));
                std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
                std::string hanzi = converter.to_bytes(unicode_char);

                if (auto hash_pos = line.find_first_of('#');
                    hash_pos != std::string::npos) {
                    auto pinyin = line.substr(colon_pos + 1, hash_pos - colon_pos - 1);
                    pinyin_dict_.emplace(std::move(hanzi),
                        string_helper::split(string_helper::trim(pinyin), ","));
                }
            }     
        }
    }
}

std::string pinyin_processor::convert_to_pinyin(const std::string& sentence) {
    auto results = split_utf8_characters(sentence);
    std::string new_sentence;
    std::string pinyin_temp;
    std::string pinyin_acronym_english;
    std::string pinyin_english;
    std::vector<std::string> pinyin_phrase_groups;
    bool last_is_utf8_character = false;
    bool last_is_single_character = false;
    for (auto const& character : results) {
        if (auto it = pinyin_dict_.find(character); it != pinyin_dict_.end()) {
            if (last_is_single_character || last_is_utf8_character) {
                new_sentence += " ";
                if (!pinyin_temp.empty())
                    pinyin_temp += " ";
            }

            auto pinyin = remove_tone(it->second[0]);
            new_sentence += pinyin;
            pinyin_temp += pinyin;

            size_t char_len = get_utf8_char_length(pinyin[0]);
            pinyin_acronym_english += pinyin.substr(0, char_len);

            pinyin_english += pinyin;

            last_is_utf8_character = true;
            last_is_single_character = false;
        } else {
            if (last_is_utf8_character) {
                new_sentence += " ";
                if (!pinyin_temp.empty()) {
                    pinyin_phrase_groups.push_back(std::move(pinyin_temp));
                }
            }

            new_sentence += character;
            pinyin_acronym_english += character;
            pinyin_english += character;
            last_is_utf8_character = false;
            last_is_single_character = true;
        }
    }

    if (!pinyin_temp.empty()) {
        pinyin_phrase_groups.push_back(std::move(pinyin_temp));
    }

    for (auto const& phrase : pinyin_phrase_groups) {
        auto words = string_helper::split(phrase, " ");
        std::string acronym;
        for (auto const& word : words) {
            if (acronym.empty()) new_sentence += " ";
            size_t char_len = get_utf8_char_length(word[0]);
            acronym += word.substr(0, char_len);
            new_sentence += word;
        }
        new_sentence += " " + acronym;
    }

    new_sentence += " " + pinyin_acronym_english;
    new_sentence += " " + pinyin_english;
    return new_sentence;
}

unsigned int pinyin_processor::hex_to_dec(const std::string& hex_str) {
    unsigned int result = 0;
    for (char c : hex_str) {
        result *= 16;
        if (c >= '0' && c <= '9') {
            result += c - '0';
        } else if (c >= 'A' && c <= 'F') {
            result += c - 'A' + 10;
        } else if (c >= 'a' && c <= 'f') {
            result += c - 'a' + 10;
        }
    }

    return result;
}

int pinyin_processor::get_utf8_char_length(unsigned char c) {
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

std::string pinyin_processor::remove_tone(const std::string& pinyin) {
    static const std::unordered_map<std::string, std::string> tone_map = {
        { "ā", "a" }, { "á", "a" }, { "ǎ", "a" }, { "à", "a" },
        { "ē", "e" }, { "é", "e" }, { "ě", "e" }, { "è", "e" },
        { "ī", "i" }, { "í", "i" }, { "ǐ", "i" }, { "ì", "i" },
        { "ō", "o" }, { "ó", "o" }, { "ǒ", "o" }, { "ò", "o" },
        { "ū", "u" }, { "ú", "u" }, { "ǔ", "u" }, { "ù", "u" },
        { "ǖ", "v" }, { "ǘ", "v" }, { "ǚ", "v" }, { "ǜ", "v" },
        { "ü", "v" }
    };

    std::string result;
    for (size_t i = 0; i < pinyin.size();) {
        size_t char_len = get_utf8_char_length(pinyin[i]);
        std::string char_str = pinyin.substr(i, char_len);
        if (auto it = tone_map.find(char_str);
            it != tone_map.end()) {
            result += it->second; // Replace with tone-free character
        } else {
            result += char_str; // Keep original character if no tone
        }

        i += char_len;
    }

    return result;
}

std::vector<std::string> pinyin_processor::split_utf8_characters(
    const std::string& sentence) {
    std::vector<std::string> result;
    std::size_t i = 0;

    while (i < sentence.length()) {
        int char_len = get_utf8_char_length(sentence[i]);
        result.push_back(sentence.substr(i, char_len));
        i += char_len;
    }   

    return result;
}

ANYTHING_NAMESPACE_END