// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_TRUST_CLIENT_H_
#define ANYTHING_TRUST_CLIENT_H_

#include <string>

#include "common/anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

/// 解析 --fd1/--fd2 命令行参数。
/// 两个参数均存在且为合法数字时写入 request_fd/response_fd 并返回 true；
/// 否则记录警告并返回 false（非 loader 启动的向后兼容路径）。
bool parse_trust_fds(int argc, char *argv[], int &request_fd, int &response_fd);

/// 向 deepin-security-loader 请求 D-Bus 信任白名单。
/// 通过 request_fd 写入请求 JSON（UniqueName + DestList），从 response_fd
/// 读取回复 JSON（{"Result":bool,"Message":string}）。
/// Result == true 返回 true；任何失败分支记录错误并返回 false。
bool request_dbus_trust(const std::string &bus_name,
                        const std::string &object_path,
                        const std::string &interface_name,
                        int request_fd,
                        int response_fd);

ANYTHING_NAMESPACE_END

#endif // ANYTHING_TRUST_CLIENT_H_
