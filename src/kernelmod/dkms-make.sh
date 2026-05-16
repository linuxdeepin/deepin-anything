#!/bin/sh
# SPDX-FileCopyrightText: 2026 guanzi008 <245205080@qq.com>
# SPDX-License-Identifier: GPL-3.0-or-later

set -eu

action="${1:-modules}"
kernel_source_dir="${2:-${kernel_source_dir:-/lib/modules/$(uname -r)/build}}"
build_dir="${3:-$(pwd)}"

case "$kernel_source_dir" in
    /*) ;;
    *) kernel_source_dir="$(pwd)/$kernel_source_dir" ;;
esac

case "$build_dir" in
    /*) ;;
    *) build_dir="$(pwd)/$build_dir" ;;
esac

kernel_source_dir="$(cd "$kernel_source_dir" && pwd -P)"
build_dir="$(cd "$build_dir" && pwd -P)"

case "$action" in
    build|modules)
        target="modules"
        ;;
    clean)
        target="clean"
        ;;
    *)
        echo "Usage: $0 {build|modules|clean} [kernel_source_dir] [build_dir]" >&2
        exit 2
        ;;
esac

compile_h="${kernel_source_dir}/include/generated/compile.h"

if [ -r "$compile_h" ] && grep -qi 'LINUX_COMPILER.*clang' "$compile_h"; then
    clang_major="$(sed -n 's/.*clang version \([0-9][0-9]*\).*/\1/p' "$compile_h" | head -n 1)"

    if [ -n "$clang_major" ] && command -v "clang-${clang_major}" >/dev/null 2>&1; then
        cc="clang-${clang_major}"
    elif command -v clang >/dev/null 2>&1; then
        cc="clang"
    else
        echo "This kernel was built with clang, but clang${clang_major:+-${clang_major}} is not installed." >&2
        exit 127
    fi

    if [ -n "$clang_major" ] && command -v "ld.lld-${clang_major}" >/dev/null 2>&1; then
        ld="ld.lld-${clang_major}"
    elif command -v ld.lld >/dev/null 2>&1; then
        ld="ld.lld"
    else
        echo "This kernel was built with clang/LLD, but ld.lld${clang_major:+-${clang_major}} is not installed." >&2
        exit 127
    fi

    clang_extra=""
    if "$cc" -Wno-gcc-install-dir-libstdcxx -Werror -mfentry -c -x c /dev/null -o /tmp/deepin-anything-clang-test.o >/dev/null 2>&1; then
        clang_extra="-Wno-gcc-install-dir-libstdcxx"
        rm -f /tmp/deepin-anything-clang-test.o
    fi

    exec make -C "$kernel_source_dir" M="$build_dir" LLVM=1 CC="$cc $clang_extra" LD="$ld" "$target"
fi

exec make -C "$kernel_source_dir" M="$build_dir" "$target"
