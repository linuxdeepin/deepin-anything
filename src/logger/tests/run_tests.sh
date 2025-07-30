#!/bin/bash

# SPDX-FileCopyrightText: 2025 UOS Technology Co., Ltd.
# SPDX-License-Identifier: GPL-3.0-or-later

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-test"

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_header() {
    echo -e "${BLUE}=====================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}=====================================${NC}"
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ️  $1${NC}"
}

show_help() {
    echo "deepin-anything-logger 测试运行脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  -h, --help          显示此帮助信息"
    echo "  -c, --clean         清理构建目录"
    echo "  -b, --build         仅构建测试"
    echo "  -t, --test          仅运行测试"
    echo "  -v, --verbose       详细输出"
    echo "  -m, --memcheck      运行内存检查"
    echo "  --coverage          生成覆盖率报告"
    echo "  --specific <test>   运行特定测试 (ConfigTest, DataTypeTest, etc.)"
    echo ""
    echo "示例:"
    echo "  $0                  # 完整构建和测试"
    echo "  $0 --clean          # 清理构建目录"
    echo "  $0 --coverage       # 生成覆盖率报告"
    echo "  $0 --specific ConfigTest  # 运行配置测试"
}

clean_build() {
    print_info "清理构建目录..."
    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
        print_success "构建目录已清理"
    else
        print_info "构建目录不存在，无需清理"
    fi
}

build_tests() {
    print_header "构建测试程序"
    
    # 创建构建目录
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    print_info "配置构建..."
    if cmake "$PROJECT_ROOT" -DENABLE_TESTING=ON -DCMAKE_BUILD_TYPE=Debug; then
        print_success "CMake配置成功"
    else
        print_error "CMake配置失败"
        exit 1
    fi
    
    print_info "编译测试程序..."
    if make -j$(nproc); then
        print_success "编译成功"
    else
        print_error "编译失败"
        exit 1
    fi
}

run_tests() {
    print_header "运行单元测试"
    
    cd "$BUILD_DIR"
    
    if [ "$VERBOSE" = "true" ]; then
        print_info "运行详细测试..."
        ctest --verbose --output-on-failure
    else
        print_info "运行测试..."
        ctest --output-on-failure
    fi
    
    if [ $? -eq 0 ]; then
        print_success "所有测试通过！"
    else
        print_error "测试失败"
        exit 1
    fi
}

run_specific_test() {
    local test_name="$1"
    print_header "运行特定测试: $test_name"
    
    cd "$BUILD_DIR"
    
    if ctest --verbose -R "$test_name"; then
        print_success "测试 $test_name 通过！"
    else
        print_error "测试 $test_name 失败"
        exit 1
    fi
}

run_memcheck() {
    print_header "运行内存检查"
    
    if ! command -v valgrind &> /dev/null; then
        print_error "valgrind 未安装，无法进行内存检查"
        exit 1
    fi
    
    cd "$BUILD_DIR"
    
    print_info "使用 Valgrind 进行内存检查..."
    if make memcheck; then
        print_success "内存检查通过！"
    else
        print_warning "内存检查发现问题，请查看报告"
    fi
}

generate_coverage() {
    print_header "生成覆盖率报告"
    
    cd "$BUILD_DIR"
    
    if ! command -v lcov &> /dev/null; then
        print_error "lcov 未安装，无法生成覆盖率报告"
        print_info "请安装 lcov: sudo apt install lcov"
        exit 1
    fi
    
    print_info "生成覆盖率报告..."
    if make coverage; then
        print_success "覆盖率报告生成完成！"
        print_info "报告位置: $BUILD_DIR/coverage_html/index.html"
        
        if command -v firefox &> /dev/null; then
            print_info "尝试使用 Firefox 打开报告..."
            firefox "$BUILD_DIR/coverage_html/index.html" &
        fi
    else
        print_error "覆盖率报告生成失败"
        exit 1
    fi
}

show_test_stats() {
    print_header "测试统计信息"
    
    cd "$BUILD_DIR"
    
    if make test_stats; then
        print_success "测试统计信息显示完成"
    else
        print_warning "无法显示测试统计信息"
    fi
}

# 解析命令行参数
CLEAN=false
BUILD_ONLY=false
TEST_ONLY=false
VERBOSE=false
MEMCHECK=false
COVERAGE=false
SPECIFIC_TEST=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -b|--build)
            BUILD_ONLY=true
            shift
            ;;
        -t|--test)
            TEST_ONLY=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -m|--memcheck)
            MEMCHECK=true
            shift
            ;;
        --coverage)
            COVERAGE=true
            shift
            ;;
        --specific)
            SPECIFIC_TEST="$2"
            shift 2
            ;;
        *)
            print_error "未知选项: $1"
            show_help
            exit 1
            ;;
    esac
done

# 执行相应操作
if [ "$CLEAN" = "true" ]; then
    clean_build
    exit 0
fi

if [ "$TEST_ONLY" = "true" ]; then
    if [ ! -d "$BUILD_DIR" ]; then
        print_error "构建目录不存在，请先构建测试"
        exit 1
    fi
    
    if [ -n "$SPECIFIC_TEST" ]; then
        run_specific_test "$SPECIFIC_TEST"
    else
        run_tests
    fi
    exit 0
fi

if [ "$BUILD_ONLY" = "true" ]; then
    build_tests
    exit 0
fi

# 默认行为：构建和测试
print_header "deepin-anything-logger 测试套件"
print_info "项目目录: $PROJECT_ROOT"
print_info "构建目录: $BUILD_DIR"

# 构建测试
build_tests

# 运行测试
if [ -n "$SPECIFIC_TEST" ]; then
    run_specific_test "$SPECIFIC_TEST"
elif [ "$MEMCHECK" = "true" ]; then
    run_memcheck
elif [ "$COVERAGE" = "true" ]; then
    run_tests  # 先运行测试
    generate_coverage
else
    run_tests
fi

# 显示统计信息
show_test_stats

print_header "测试完成"
print_success "所有操作成功完成！" 