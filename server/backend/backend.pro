TEMPLATE = lib
TARGET = deepin-anything-server-lib
QT += dbus concurrent dtkcore
QT -= gui
CONFIG += link_pkgconfig
PKGCONFIG += udisks2-qt5 mount

DEFINES += QT_MESSAGELOGCONTEXT

INCLUDEPATH += $$PWD/../../kernelmod $$PWD/lib
INCLUDEPATH += $$PWD/../../library/inc
INCLUDEPATH += dbusservice

SOURCES += \
    lib/dasplugin.cpp \
    lib/dasfactory.cpp \
    lib/dasinterface.cpp \
    lib/daspluginloader.cpp \
    lib/lftmanager.cpp \
    lib/lftdisktool.cpp \
    server.cpp \
    anythingbackend.cpp

HEADERS += \
    lib/dasdefine.h \
    lib/dasplugin.h \
    lib/dasfactory.h \
    lib/dasinterface.h \
    lib/daspluginloader.h \
    lib/lftmanager.h \
    lib/lftdisktool.h \
    server.h \
    anythingbackend.h \
    anythingexport.h

CONFIG(debug, debug|release) {
    LIBS += -L$$_PRO_FILE_PWD_/../../library/bin/debug -lanything
    DEPENDPATH += $$_PRO_FILE_PWD_/../../library/bin/debug
    unix:QMAKE_RPATHDIR += $$_PRO_FILE_PWD_/../../library/bin/debug
} else {
    LIBS += -L$$_PRO_FILE_PWD_/../../library/bin/release -lanything
}

isEmpty(LIB_INSTALL_DIR) {
    LIB_INSTALL_DIR = $$[QT_INSTALL_LIBS]
}

DEFINES += QMAKE_VERSION=\\\"$$VERSION\\\"

PLUGINDIR = $$LIB_INSTALL_DIR/$${TARGET}/plugins

readme.files += README.txt
readme.path = $$PLUGINDIR/handlers

CONFIG(debug, release|debug) {
    PLUGINDIR = $$_PRO_FILE_PWD_/../plugins:$$PLUGINDIR
}

DEFINES += PLUGINDIR=\\\"$$PLUGINDIR\\\"

target.path = $$LIB_INSTALL_DIR

isEmpty(PREFIX): PREFIX = /usr

includes.files += \
    lib/dasdefine.h \
    lib/dasfactory.h \
    lib/dasplugin.h \
    lib/dasinterface.h \
    lib/lftmanager.h \
    anythingexport.h

includes.path = $$PREFIX/include/deepin-anything-server-lib

dbus.files = dbusservice/com.deepin.anything.xml
dbus.header_flags += -l LFTManager -i $$PWD/lib/lftmanager.h
dbus.source_flags += -l LFTManager

DBUS_ADAPTORS += dbus

dbus_xmls.path = /usr/share/dbus-1/interfaces
dbus_xmls.files = $$dbus.files

dbus_config.path = /etc/dbus-1/system.d
dbus_config.files = dbusservice/com.deepin.anything.conf

INSTALLS += target includes readme dbus_xmls dbus_config
CONFIG += create_pc create_prl no_install_prl

QMAKE_PKGCONFIG_LIBDIR = $$target.path
QMAKE_PKGCONFIG_VERSION = $$VERSION
QMAKE_PKGCONFIG_DESTDIR = pkgconfig
QMAKE_PKGCONFIG_NAME = $$TARGET
QMAKE_PKGCONFIG_DESCRIPTION = Deepin anything backend library
QMAKE_PKGCONFIG_INCDIR = $$includes.path
