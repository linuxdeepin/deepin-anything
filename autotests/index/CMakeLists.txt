# Unit tests for the deepin-anything-index service.
# Single white-box target: compiles the index sources into the test binary
# (stub-ext needs direct access), plus the extractor pipe tests.
#
# Runs via: ./autotests/run-ut.sh            (build + run)
#           ./autotests/run-ut.sh --coverage (with lcov HTML report)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Concurrent Test)
find_package(Dtk6 COMPONENTS Core REQUIRED)
find_package(KF6IdleTime REQUIRED)
find_package(dfm6-search REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(Lucene REQUIRED IMPORTED_TARGET liblucene++ liblucene++-contrib)
pkg_check_modules(MOUNT REQUIRED mount IMPORTED_TARGET)
pkg_check_modules(NL REQUIRED IMPORTED_TARGET libnl-3.0 libnl-genl-3.0)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gio-2.0)

file(GLOB_RECURSE TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# ANYTHING_INDEX_SOURCES comes from the src/index subproject (CACHE INTERNAL);
# the D-Bus adaptors are provided by the index_adaptors static library target.
anything_add_test(ut-anything-index
    SOURCES ${TEST_SOURCES} ${ANYTHING_INDEX_SOURCES}
    LINK_LIBRARIES common index_adaptors deepin-anything-dispatcher deepin-anything-extractor-lib dfm6-search
                   Qt6::Core Qt6::DBus Qt6::Gui Qt6::Concurrent
                   Dtk6::Core KF6::IdleTime
                   PkgConfig::Lucene PkgConfig::MOUNT PkgConfig::NL PkgConfig::GLIB
    INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/src/index
                 ${CMAKE_SOURCE_DIR}/src/index/dbus
                 ${CMAKE_SOURCE_DIR}/src/extractor/libextractor
                 ${CMAKE_SOURCE_DIR}/src/dispatcher
)

target_compile_definitions(ut-anything-index PRIVATE
    ANYTHING_EXTRACTOR_TOOL="/usr/libexec/deepin-anything-extractor"
    PINYIN_DICT_INSTALL_DIR="/usr/share/deepin-anything-index"
)
