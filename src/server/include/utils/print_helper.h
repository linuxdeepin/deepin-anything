// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_PRINT_HELPER_H_
#define ANYTHING_PRINT_HELPER_H_

#include <iostream>

#include "common/anything_fwd.hpp"
#include "common/file_record.h"

ANYTHING_NAMESPACE_BEGIN

inline void print(const file_record& record) {
    std::cout << "file_name: " << record.file_name <<
        " full_path: " << record.full_path <<
        " file_type: " << record.file_type << "\n";
}

ANYTHING_NAMESPACE_END

#endif // ANYTHING_PRINT_HELPER_H_