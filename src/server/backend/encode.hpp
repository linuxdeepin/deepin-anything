#ifndef ANYTHING_ENCODE_HPP_
#define ANYTHING_ENCODE_HPP_

#include <locale>
#include <iostream>

#include "anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

inline void set_encode(const char* std_name) {
    std::ios_base::sync_with_stdio(false);
    std::wcout.imbue(std::locale(std_name));
}

ANYTHING_NAMESPACE_END

#endif // ANYTHING_ENCODE_HPP_