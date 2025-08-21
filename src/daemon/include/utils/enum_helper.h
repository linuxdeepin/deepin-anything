// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_ENUM_HELPER_H_
#define ANYTHING_ENUM_HELPER_H_

#include "common/anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

template<typename E>
constexpr auto to_underlying(E e) noexcept {
    return static_cast<std::underlying_type_t<E>>(e);
}

ANYTHING_NAMESPACE_END

#endif // ANYTHING_ENUM_HELPER_H_