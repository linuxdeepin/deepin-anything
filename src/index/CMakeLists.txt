cmake_minimum_required(VERSION 3.10)

project(deepin-anything-index CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Concurrent)
find_package(Dtk6 COMPONENTS Core)
find_package(dfm6-search REQUIRED)
find_package(KF6IdleTime REQUIRED)

pkg_check_modules(Lucene REQUIRED IMPORTED_TARGET liblucene++ liblucene++-contrib)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gio-2.0)
pkg_check_modules(MOUNT REQUIRED mount IMPORTED_TARGET)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

set(INDEX_SOURCES
    core/indexruntime.cpp
    core/indexcontext.h
    task/backlogtracker.cpp
    task/fileprovider.cpp
    task/indexcontentmigrator.cpp
    task/indextask.cpp
    task/moveprocessor.cpp
    task/progressnotifier.cpp
    task/taskhandler.cpp
    task/taskmanager.cpp
    task/taskqueueutils.cpp
    document/contentdocumentbuilder.cpp
    document/filenamedocumentbuilder.cpp
    document/ocrdocumentbuilder.cpp
    fsmonitor/fseventcollector.cpp
    fsmonitor/fseventcontroller.cpp
    fsmonitor/fsmonitor.cpp
    fsmonitor/fsmonitorworker.cpp
    fsmonitor/inotifyfilesystemwatcher.cpp
    fsmonitor/vfsmonitorwatcher.cpp
    profile/indexprofile.cpp
    profile/lowercasengramanalyzer.cpp
    state/indexstatestore.cpp
    env/envdetector.cpp
    env/idlemonitor.cpp
    env/loadmonitor.cpp
    env/powermonitor.cpp
    extractor/contentdeduplication.cpp
    extractor/ocrdeduplication.cpp
    extractor/processextractor.cpp
    utils/docutils.cpp
    utils/filehash.cpp
    utils/filetypemapper.cpp
    utils/indextraverseutils.cpp
    utils/indexutility.cpp
    utils/pathexcludematcher.cpp
    utils/pinyinprocessor.cpp
    utils/systemdcpuutils.cpp
    utils/textindexconfig.cpp
    utils/threadscheduling.cpp
    utils/trustclient.cpp
    dbus/textindexdbus.cpp
    dbus/ocrindexdbus.cpp
    dbus/filenameindexdbus.cpp
    serviceentry.cpp
)
list(TRANSFORM INDEX_SOURCES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)
file(GLOB_RECURSE INDEX_HEADERS CONFIGURE_DEPENDS "*.h")

# D-Bus adaptors: the three interface XMLs are the single source of truth.
set(DBUS_INTERFACE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/dbus/org.deepin.Filemanager.TextIndex.xml)
set(OCR_DBUS_INTERFACE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/dbus/org.deepin.Filemanager.OcrIndex.xml)
set(FILENAME_DBUS_INTERFACE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/dbus/org.deepin.Filemanager.FileNameIndex.xml)

qt6_add_dbus_adaptor(INDEX_ADAPTOR_SOURCES ${DBUS_INTERFACE_FILE}
    dbus/textindexdbus.h TextIndexDBus)
qt6_add_dbus_adaptor(OCR_ADAPTOR_SOURCES ${OCR_DBUS_INTERFACE_FILE}
    dbus/ocrindexdbus.h OcrIndexDBus)
qt6_add_dbus_adaptor(FILENAME_ADAPTOR_SOURCES ${FILENAME_DBUS_INTERFACE_FILE}
    dbus/filenameindexdbus.h FileNameIndexDBus)

# Pin the generated adaptor paths (the macro's relative output is ambiguous
# across scopes; the tests subdirectory reuses these variables).
set(INDEX_ADAPTOR_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/textindexadaptor.cpp)
set(OCR_ADAPTOR_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/ocrindexadaptor.cpp)
set(FILENAME_ADAPTOR_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/filenameindexadaptor.cpp)
set_source_files_properties(${INDEX_ADAPTOR_SOURCES} ${OCR_ADAPTOR_SOURCES}
    ${FILENAME_ADAPTOR_SOURCES} PROPERTIES GENERATED TRUE)

# Bundle the three adaptors into a small static library so both the service
# binary and the white-box test target (in a subdirectory) can consume the
# generated files without cross-directory custom-command tricks.
add_library(index_adaptors STATIC
    ${INDEX_ADAPTOR_SOURCES}
    ${OCR_ADAPTOR_SOURCES}
    ${FILENAME_ADAPTOR_SOURCES}
)
target_include_directories(index_adaptors PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(index_adaptors PUBLIC Qt6::Core Qt6::DBus dfm6-search)

add_executable(deepin-anything-index
    ${INDEX_SOURCES}
    ${INDEX_HEADERS}
    main.cpp
)

target_include_directories(deepin-anything-index PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/dbus
    ${CMAKE_SOURCE_DIR}/src/dispatcher
)

target_compile_definitions(deepin-anything-index PRIVATE
    ANYTHING_EXTRACTOR_TOOL="${CMAKE_INSTALL_FULL_LIBEXECDIR}/deepin-anything-extractor"
    PINYIN_DICT_INSTALL_DIR="${CMAKE_INSTALL_FULL_DATADIR}/deepin-anything-index"
)

target_link_libraries(deepin-anything-index PRIVATE
    common
    index_adaptors
    deepin-anything-dispatcher
    deepin-anything-extractor-lib
    dfm6-search
    Dtk6::Core
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Concurrent
    PkgConfig::Lucene
    PkgConfig::MOUNT
    PkgConfig::GLIB
    KF6::IdleTime
    PkgConfig::SYSTEMD
)

# The extractor communication library lives in the standard libdir; no RPATH needed.
target_compile_options(deepin-anything-index PRIVATE -Wall -Wextra)

install(TARGETS deepin-anything-index DESTINATION libexec/deepin)
# systemd only searches /usr/lib/systemd/user for user units (no multiarch variant).
install(FILES deepin-anything-index.service DESTINATION lib/systemd/user)
install(FILES assets/dbus/org.deepin.Filemanager.TextIndex.service
    DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/dbus-1/services)
# The three interface XMLs in assets/dbus/ are the canonical source for
# qt6_add_dbus_adaptor codegen only — they are NOT installed from this
# package; dde-file-manager publishes its frozen copies (ADR-0002).
install(FILES ${CMAKE_SOURCE_DIR}/config/pinyin.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/deepin-anything-index)

# White-box test support: expose the index source list to the root-level
# autotests/index suite (ut-anything-index compiles these sources directly).
set(ANYTHING_INDEX_SOURCES "${INDEX_SOURCES}" CACHE INTERNAL "deepin-anything-index sources for ut-anything-index")
