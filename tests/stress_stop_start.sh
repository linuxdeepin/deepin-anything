#!/bin/bash
# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# deepin-anything 服务停启压测脚本
#
# 循环验证 server/daemon 的健康状态、正常关闭、恢复运行
# 整体以 root 身份运行；daemon 作为用户级 systemd 服务，
# 通过 runuser 切换到目标用户身份执行 systemctl --user 操作。
#
# 用法:
#   sudo ./tests/stress_stop_start.sh            # 无限循环，Ctrl+C 停止
#   sudo ./tests/stress_stop_start.sh 20         # 固定 20 轮
#
# 中断行为: 收到 SIGINT/SIGTERM 后立即中断当前等待并退出，不等待
# 当前阶段完成；服务可能停留在停止或中间状态 (这是预期的，用户要退出)。
#
# 退出码:
#   0   正常完成
#   1   参数/环境错误
#   2   检查阶段失败 (server/daemon 未运行或状态非 monitoring)
#   3   停止阶段失败 (服务未在预期时间内退出)
#   4   恢复阶段失败 (daemon 未在预期时间内进入 monitoring)
#   5   压测结束但有失败轮次
#   130 收到中断信号提前退出
set -u
umask 0022

# ===================== 配置 =====================

SERVER_SERVICE="deepin-anything-server"     # 系统服务 (root)
DAEMON_SERVICE="deepin-anything-daemon"     # 用户服务

# 停止阶段: 先停 server，间隔 1s，再停 daemon；两者总等待上限
STOP_INTERVAL_S=1
STOP_WAIT_S=2

# 恢复阶段: 先启 server，间隔 1s，再启 daemon；等待 daemon 进入 monitoring 上限
START_INTERVAL_S=1
MONITOR_WAIT_S=600                          # 10 min

# 状态文件根目录，会按运行用户 uid 拼接: /run/user/<uid>/deepin-anything-server/status.json
STATUS_FILE_DIR_PREFIX="/run/user"

# 中断返回码 (收到 SIGINT/SIGTERM 时由各阶段返回，主循环据此跳过恢复直接退出)
RC_INTERRUPTED=130

# ===================== 日志 =====================

# 颜色 (非 tty 自动关闭)
if [ -t 1 ]; then
    C_RED=$'\033[31m'; C_GRN=$'\033[32m'; C_YEL=$'\033[33m'
    C_CYN=$'\033[36m'; C_RST=$'\033[0m'
else
    C_RED=""; C_GRN=""; C_YEL=""; C_CYN=""; C_RST=""
fi

ts() { date '+%Y-%m-%d %H:%M:%S'; }

log_info()  { printf '%s[%s INFO %s] %s%s\n'  "${C_CYN}" "$(ts)" "${C_RST}" "$*" "${C_RST}"; }
log_ok()    { printf '%s[%s OK   %s] %s%s\n'  "${C_GRN}" "$(ts)" "${C_RST}" "$*" "${C_RST}"; }
log_warn()  { printf '%s[%s WARN %s] %s%s\n'  "${C_YEL}" "$(ts)" "${C_RST}" "$*" "${C_RST}"; }
log_err()   { printf '%s[%s ERROR%s] %s%s\n' "${C_RED}" "$(ts)" "${C_RST}" "$*" "${C_RST}" >&2; }

# ===================== 环境 =====================

# 整体以 root 运行 (server 是系统服务需要 root)
if [ "$(id -u)" -ne 0 ]; then
    log_err "本脚本需要以 root 身份运行 (server 为系统服务)"
    exit 1
fi

# 自动检测当前登录用户: 取 tty/pts 上活跃会话对应的 uid，默认 1000
# 优先取环境变量 SUDO_USER (sudo 运行)，其次取一个有 /run/user 目录的登录用户
detect_user() {
    local uid

    # 1) sudo 透传的原始用户
    if [ -n "${SUDO_USER:-}" ] && [ "${SUDO_USER}" != "root" ]; then
        uid="$(id -u "${SUDO_USER}" 2>/dev/null)" && [ -n "$uid" ] && { echo "$uid"; return; }
    fi

    # 2) 查找当前有活跃 systemd 用户实例的登录用户 (有 /run/user/<uid>/bus)
    local best_uid="" best_time=0
    for d in /run/user/[0-9]*; do
        [ -d "$d/bus" ] || continue
        local u="${d##*/}"
        # 该用户是否为活跃登录会话
        if loginctl show-user "$u" 2>/dev/null | grep -q '^State=active'; then
            echo "$u"
            return
        fi
        [ -z "$best_uid" ] && best_uid="$u"
    done

    # 3) 回退: 取第一个存在的 /run/user/<uid>
    if [ -n "$best_uid" ]; then
        echo "$best_uid"
        return
    fi

    # 4) 最终回退 1000
    echo 1000
}

RUN_UID="$(detect_user)"
RUN_USER="$(id -u "${RUN_UID}" >/dev/null 2>&1 && getent passwd "${RUN_UID}" | cut -d: -f1 || echo uos)"
RUN_DIR="/run/user/${RUN_UID}"
STATUS_FILE="${STATUS_FILE_DIR_PREFIX}/${RUN_UID}/deepin-anything-server/status.json"

log_info "运行用户: ${RUN_USER} (uid=${RUN_UID}), 运行目录: ${RUN_DIR}"

# 前置校验: 目标用户必须有活跃的 user-bus 才能操作其用户服务实例
if [ ! -S "${RUN_DIR}/bus" ]; then
    log_err "用户 ${RUN_USER} (uid=${RUN_UID}) 无 user-bus (${RUN_DIR}/bus)"
    log_err "请确保该用户已活跃登录 (有 systemd 用户会话) 后再运行"
    exit 1
fi

# ===================== 用户级 systemctl 包装 =====================
# root 通过 runuser 切换到目标用户身份执行 systemctl --user。
# 仅设置 XDG/DBUS 环境变量对 root 不够: systemd 拒绝非会话属主的
# 总线连接 (报 "不允许的操作")，必须以目标用户身份运行。

user_systemctl() {
    runuser -u "${RUN_USER}" -- env \
        XDG_RUNTIME_DIR="${RUN_DIR}" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=${RUN_DIR}/bus" \
        systemctl --user "$@"
}

server_systemctl() {
    systemctl "$@"
}

# ===================== 检查原语 =====================

check_server_running() {
    server_systemctl is-active --quiet "$SERVER_SERVICE"
}

check_daemon_running() {
    user_systemctl is-active --quiet "$DAEMON_SERVICE"
}

# daemon 监控状态: status.json 含 "monitoring"
check_daemon_monitoring() {
    [ -f "$STATUS_FILE" ] || return 1
    grep -q '"monitoring"' "$STATUS_FILE" 2>/dev/null
}

# daemon 监控状态 (新鲜度感知): status.json 含 monitoring 且 mtime 晚于基线时间戳
# 基线由 MONITOR_BASELINE_NS 全局变量提供 (启动 daemon 前设置)
# 用于避免 daemon 停止后残留的旧 status.json 被误判为已进入 monitoring
MONITOR_BASELINE_S=0
check_daemon_monitoring_fresh() {
    [ -f "$STATUS_FILE" ] || return 1
    grep -q '"monitoring"' "$STATUS_FILE" 2>/dev/null || return 1
    # status.json 的 mtime (秒级) 必须严格晚于启动 daemon 前记录的基线
    local mtime_s
    mtime_s=$(stat -c '%Y' "$STATUS_FILE" 2>/dev/null) || return 1
    [ "$mtime_s" -gt "$MONITOR_BASELINE_S" ] 2>/dev/null
}

# 全局中断标志: SIGINT/SIGTERM 的 trap 置 1，各轮询循环据此立即退出
INTERRUPTED=0

# 可中断 sleep: 收到中断信号立即返回，供阶段间停顿使用
sleep_intr() {
    local total="$1"
    local slept=0
    while [ "$slept" -lt "$total" ]; do
        [ "$INTERRUPTED" -ne 0 ] && return 1
        sleep 0.5
        slept=$((slept + 1))
    done
    return 0
}

# 等待条件成立: 轮询直到条件满足或超时；收到中断信号立即退出
# 用法: wait_for_condition <描述> <超时秒> <函数...>
wait_for_condition() {
    local desc="$1"; shift
    local timeout="$1"; shift
    local start now elapsed
    start=$(date +%s)
    while true; do
        [ "$INTERRUPTED" -ne 0 ] && { log_warn "等待中断: ${desc}"; return "$RC_INTERRUPTED"; }
        if "$@"; then
            return 0
        fi
        now=$(date +%s); elapsed=$((now - start))
        if [ "$elapsed" -ge "$timeout" ]; then
            log_err "等待超时 (${timeout}s): ${desc}"
            return 1
        fi
        sleep 0.5
    done
}

# 反向等待: 等待条件不成立 (服务停止)；收到中断信号立即退出
# 用法: wait_for_not <描述> <超时秒> <函数...>
wait_for_not() {
    local desc="$1"; shift
    local timeout="$1"; shift
    local start now elapsed
    start=$(date +%s)
    while true; do
        [ "$INTERRUPTED" -ne 0 ] && { log_warn "等待中断: ${desc}"; return "$RC_INTERRUPTED"; }
        if ! "$@"; then
            return 0
        fi
        now=$(date +%s); elapsed=$((now - start))
        if [ "$elapsed" -ge "$timeout" ]; then
            log_err "等待超时 (${timeout}s): ${desc}"
            return 1
        fi
        sleep 0.5
    done
}

# ===================== 压测三个阶段 =====================

# 阶段 1: 检查服务正常运行
phase_check_health() {
    log_info "[阶段1] 检查服务正常运行"

    if ! check_server_running; then
        log_err "server (${SERVER_SERVICE}) 未运行"
        server_systemctl status "$SERVER_SERVICE" --no-pager -l | log_err
        return 2
    fi
    log_ok "server 运行中"

    if ! check_daemon_running; then
        log_err "daemon (${DAEMON_SERVICE}) 未运行"
        user_systemctl status "$DAEMON_SERVICE" --no-pager -l | log_err
        return 2
    fi
    log_ok "daemon 运行中"

    if ! check_daemon_monitoring; then
        log_err "daemon 未进入 monitoring 状态 (status.json 不含 monitoring)"
        [ -f "$STATUS_FILE" ] && { log_err "当前 status.json:"; cat "$STATUS_FILE" >&2; }
        return 2
    fi
    log_ok "daemon 处于 monitoring 状态"
    return 0
}

# 阶段 2: 测试服务正常关闭
phase_stop_services() {
    log_info "[阶段2] 测试服务正常关闭"

    # 2.1 先停 server，间隔 1s，再停 daemon
    log_info "停止 server (${SERVER_SERVICE}) ..."
    server_systemctl stop "$SERVER_SERVICE" || {
        log_err "停止 server 失败"
        return 3
    }
    sleep_intr "$STOP_INTERVAL_S"

    log_info "停止 daemon (${DAEMON_SERVICE}) ..."
    user_systemctl stop "$DAEMON_SERVICE" || {
        log_warn "停止 daemon 返回非零 (可能已随 server 退出)，继续等待"
    }

    # 2.2 等待两者均停止，超时报错
    # 由于 server/daemon 间无依赖关系，并行等待
    log_info "等待 server 与 daemon 完全停止 (上限 ${STOP_WAIT_S}s) ..."

    local server_ok=0 daemon_ok=0
    local start now elapsed
    start=$(date +%s)
    while true; do
        [ "$INTERRUPTED" -ne 0 ] && { log_warn "停止等待被中断"; return "$RC_INTERRUPTED"; }
        check_server_running || server_ok=1
        check_daemon_running || daemon_ok=1
        if [ "$server_ok" -eq 1 ] && [ "$daemon_ok" -eq 1 ]; then
            log_ok "server 与 daemon 均已停止"
            return 0
        fi
        now=$(date +%s); elapsed=$((now - start))
        if [ "$elapsed" -ge "$STOP_WAIT_S" ]; then
            [ "$server_ok" -ne 1 ] && log_err "server 未在 ${STOP_WAIT_S}s 内停止"
            [ "$daemon_ok" -ne 1 ] && log_err "daemon 未在 ${STOP_WAIT_S}s 内停止"
            return 3
        fi
        sleep 0.3
    done
}

# 阶段 3: 恢复服务运行
phase_start_services() {
    log_info "[阶段3] 恢复服务运行"

    # 3.1 先启 server，间隔 1s，再启 daemon
    log_info "启动 server (${SERVER_SERVICE}) ..."
    server_systemctl start "$SERVER_SERVICE" || {
        log_err "启动 server 失败"
        server_systemctl status "$SERVER_SERVICE" --no-pager -l | log_err
        return 4
    }
    sleep_intr "$START_INTERVAL_S"

    # 在启动 daemon 前记录基线时间戳，用于区分 daemon 重写的新 status.json
    # 与停止后残留的旧 status.json (后者 mtime 不会更新)
    MONITOR_BASELINE_S=$(date +%s)
    log_info "启动 daemon (${DAEMON_SERVICE}) ... (baseline ts=${MONITOR_BASELINE_S})"
    user_systemctl start "$DAEMON_SERVICE" || {
        log_err "启动 daemon 失败"
        user_systemctl status "$DAEMON_SERVICE" --no-pager -l | log_err
        return 4
    }

    # 3.2 等待 daemon 进入 monitoring 状态，超时 10min
    # 使用新鲜度感知检查: 要求 status.json 的 mtime 晚于启动前的基线，
    # 避免 daemon 停止时残留的旧 status.json (仍含 monitoring) 导致误判
    log_info "等待 daemon 进入 monitoring 状态 (上限 $((MONITOR_WAIT_S/60))min) ..."
    wait_for_condition "daemon 进入 monitoring" "$MONITOR_WAIT_S" check_daemon_monitoring_fresh
    local rc=$?
    if [ "$rc" -eq "$RC_INTERRUPTED" ]; then
        return "$RC_INTERRUPTED"
    elif [ "$rc" -ne 0 ]; then
        log_err "daemon 未在 ${MONITOR_WAIT_S}s 内进入 monitoring"
        log_err "当前 daemon 状态:"
        user_systemctl status "$DAEMON_SERVICE" --no-pager -l 2>&1 | log_err
        [ -f "$STATUS_FILE" ] && { log_err "当前 status.json:"; cat "$STATUS_FILE" >&2; }
        return 4
    fi
    log_ok "daemon 已进入 monitoring 状态"
    return 0
}

# ===================== 单轮压测 =====================

run_one_round() {
    local round="$1"
    local t0 t1
    printf '\n%s══════════════════════════════════════════════════\n' "${C_CYN}"
    printf '%s  第 %s 轮压测开始  %s\n' "${C_CYN}" "$round" "${C_RST}"
    printf '%s══════════════════════════════════════════════════%s\n' "${C_CYN}" "${C_RST}"
    t0=$(date +%s)

    phase_check_health || return $?
    phase_stop_services || return $?
    phase_start_services || return $?

    t1=$(date +%s)
    log_ok "第 ${round} 轮压测通过 (用时 $((t1 - t0))s)"
    return 0
}

# ===================== 主流程 =====================

main() {
    local max_rounds="${1:-0}"   # 0 = 无限
    local round=0
    local pass=0 fail=0
    local start_ts end_ts

    start_ts=$(date +%s)

    if [ "$max_rounds" -gt 0 ]; then
        log_info "压测计划: 固定 ${max_rounds} 轮"
    else
        log_info "压测计划: 无限循环，Ctrl+C 停止"
    fi

    # 优雅处理 Ctrl+C: 置 INTERRUPTED，各轮询循环据此立即退出
    trap 'INTERRUPTED=1; log_warn "收到中断信号，立即退出当前等待..."' INT TERM

    while true; do
        [ "$INTERRUPTED" -ne 0 ] && break
        round=$((round + 1))

        if [ "$max_rounds" -gt 0 ] && [ "$round" -gt "$max_rounds" ]; then
            break
        fi

        if run_one_round "$round"; then
            pass=$((pass + 1))
        else
            local rc=$?
            # 中断导致的退出不计入失败，直接结束
            if [ "$rc" -eq "$RC_INTERRUPTED" ] || [ "$INTERRUPTED" -ne 0 ]; then
                log_warn "第 ${round} 轮因中断信号提前结束，停止压测"
                break
            fi
            fail=$((fail + 1))
            log_err "第 ${round} 轮压测失败 (退出码 ${rc})"
            # 失败后尝试恢复到健康状态，避免下轮因环境残留失败
            log_warn "尝试恢复服务到健康状态 ..."
            server_systemctl start "$SERVER_SERVICE" 2>/dev/null
            sleep_intr "$START_INTERVAL_S"
            user_systemctl start "$DAEMON_SERVICE" 2>/dev/null
            MONITOR_BASELINE_S=$(date +%s)
            wait_for_condition "daemon 进入 monitoring" "$MONITOR_WAIT_S" check_daemon_monitoring_fresh 2>/dev/null \
                && log_ok "已恢复到 monitoring 状态" \
                || log_warn "恢复失败，后续轮次可能受影响"
        fi
    done

    end_ts=$(date +%s)
    printf '\n%s══════════════════════════════════════════════════%s\n' "${C_CYN}" "${C_RST}"
    log_info "压测结束: 共 ${round} 轮, 通过 ${pass}, 失败 ${fail}, 总用时 $((end_ts - start_ts))s"
    printf '%s══════════════════════════════════════════════════%s\n' "${C_CYN}" "${C_RST}"

    [ "$INTERRUPTED" -ne 0 ] && exit 130
    [ "$fail" -eq 0 ] && exit 0 || exit 5
}

main "$@"
