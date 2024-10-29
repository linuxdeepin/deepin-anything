#ifndef ANYTHING_GENL_PARSER_HPP_
#define ANYTHING_GENL_PARSER_HPP_

#include <optional>
#include <type_traits>

#include "anything/common/anything_fwd.hpp"


ANYTHING_NAMESPACE_BEGIN

using nla_u8     = uint8_t;
using nla_u16    = uint16_t;
using nla_u32    = uint32_t;
using nla_u64    = uint64_t;
using nla_string = char*;


class nla_parser {
public:
    explicit nla_parser(nlattr** tb) : tb_{tb} {}

    template<typename T>
    std::optional<T> get_value(unsigned int attr) const {
        if (!tb_[attr])
		    return std::nullopt;
        
        if constexpr (std::is_same_v<T, nla_string>)
            return nla_get_string(tb_[attr]);
        else if constexpr (std::is_same_v<T, nla_u8>)
            return nla_get_u8(tb_[attr]);
        else if constexpr (std::is_same_v<T, nla_u16>)
            return nla_get_u16(tb_[attr]);
        else if constexpr (std::is_same_v<T, nla_u32>)
            return nla_get_u32(tb_[attr]);
        else if constexpr (std::is_same_v<T, nla_u64>)
            return nla_get_u64(tb_[attr]);
        else
            return std::nullopt;
    }

private:
    nlattr** tb_;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_GENL_PARSER_HPP_