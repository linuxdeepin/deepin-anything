// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_SYS_H_
#define ANYTHING_SYS_H_

#include <string>

#include "common/anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

std::string get_home_directory();
std::string get_user_cache_directory();
std::string get_sys_cache_directory();

ANYTHING_NAMESPACE_END

#endif // ANYTHING_SYS_H_