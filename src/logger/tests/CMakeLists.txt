# SPDX-FileCopyrightText: 2025 UOS Technology Co., Ltd.
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.10)

# 启用测试
enable_testing()

# 设置测试相关的编译选项
if(ENABLE_TESTING)
    message(STATUS "启用单元测试构建")
    
    # 添加覆盖率编译选项
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
        add_link_options(--coverage)
    endif()
    
    # 查找测试依赖
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLIB2 REQUIRED glib-2.0)
    pkg_check_modules(GIO2 REQUIRED gio-2.0)
    pkg_check_modules(LIBNL REQUIRED libnl-3.0 libnl-genl-3.0)
    
    # 设置测试包含目录
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)
    include_directories(${GLIB2_INCLUDE_DIRS})
    include_directories(${GIO2_INCLUDE_DIRS})
    include_directories(${LIBNL_INCLUDE_DIRS})
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../kernelmod)
    
    # 设置测试链接库
    set(TEST_LIBRARIES 
        ${GLIB2_LIBRARIES}
        ${GIO2_LIBRARIES}
        ${LIBNL_LIBRARIES}
        m
        pthread
    )
    
    # 设置测试源文件目录
    set(LOGGER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
    
    # 通用测试源文件
    set(COMMON_TEST_SOURCES
        ${LOGGER_SOURCE_DIR}/log.c
        ${LOGGER_SOURCE_DIR}/datatype.c
    )
    
    # 事件日志模块测试
    add_executable(test_event_logger
        test_event_logger.c
        ${LOGGER_SOURCE_DIR}/event_logger.c
        ${LOGGER_SOURCE_DIR}/file_log.c
        ${COMMON_TEST_SOURCES}
    )
    target_link_libraries(test_event_logger ${TEST_LIBRARIES})
    add_test(NAME EventLoggerTest COMMAND test_event_logger)
    
    # 文件日志模块测试
    add_executable(test_file_log
        test_file_log.c
        ${LOGGER_SOURCE_DIR}/file_log.c
        ${COMMON_TEST_SOURCES}
    )
    target_link_libraries(test_file_log ${TEST_LIBRARIES})
    add_test(NAME FileLogTest COMMAND test_file_log)
    
    # 设置测试环境变量
    set_tests_properties(EventLoggerTest PROPERTIES
        ENVIRONMENT "G_MESSAGES_DEBUG=all"
    )
    set_tests_properties(FileLogTest PROPERTIES
        ENVIRONMENT "G_MESSAGES_DEBUG=all"
    )
    
    # 覆盖率报告目标
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        find_program(GCOV_PATH gcov)
        find_program(LCOV_PATH lcov)
        find_program(GENHTML_PATH genhtml)
        
        if(LCOV_PATH AND GENHTML_PATH)
            # 清理覆盖率数据
            add_custom_target(coverage_clean
                COMMAND ${LCOV_PATH} --directory . --zerocounters
                COMMENT "清理覆盖率计数器"
            )
            
            # 生成覆盖率报告
            add_custom_target(coverage
                # 运行测试
                COMMAND ${CMAKE_CTEST_COMMAND} --verbose
                
                # 收集覆盖率数据
                COMMAND ${LCOV_PATH} --directory . --capture --output-file coverage.info
                
                # 过滤系统文件和测试文件
                COMMAND ${LCOV_PATH} --remove coverage.info 
                    '/usr/*' 
                    '*/tests/*'
                    '*/build/*'
                    --output-file coverage_filtered.info
                
                # 生成HTML报告
                COMMAND ${GENHTML_PATH} coverage_filtered.info --output-directory coverage_html
                
                COMMENT "生成测试覆盖率报告"
                DEPENDS coverage_clean
            )
            
            message(STATUS "覆盖率报告将生成到 coverage_html/ 目录")
        else()
            message(WARNING "未找到lcov或genhtml，无法生成覆盖率报告")
        endif()
    endif()
    
    # 内存泄漏检测
    find_program(VALGRIND_PATH valgrind)
    if(VALGRIND_PATH)
        add_custom_target(memcheck
            COMMAND ${CMAKE_CTEST_COMMAND} 
                --test-action memcheck
                --force-new-ctest-process
                --output-on-failure
            COMMENT "使用Valgrind进行内存泄漏检测"
        )
        message(STATUS "可使用 'make memcheck' 进行内存泄漏检测")
    endif()
    
endif()

# 便利目标
add_custom_target(all_tests
    DEPENDS 
        test_event_logger
        test_file_log
    COMMENT "构建所有测试"
)

# 显示测试统计信息
add_custom_target(test_stats
    COMMAND echo "=== 测试统计信息 ==="
    COMMAND echo "事件日志测试: test_event_logger"
    COMMAND echo "文件日志测试: test_file_log"
    COMMAND echo "==================="
    COMMENT "显示测试模块信息"
) 