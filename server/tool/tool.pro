TARGET = deepin-anything-tool
QT += core dbus dtkcore
QT -= gui
TEMPLATE = app

include(../common.pri)

SOURCES += \
    main.cpp

CONFIG(debug, debug|release) {
    QMAKE_RPATHLINKDIR += $$_PRO_FILE_PWD_/../../library/bin/debug
} else {
    QMAKE_RPATHLINKDIR += $$_PRO_FILE_PWD_/../../library/bin/release
}

INCLUDEPATH += ../lib
LIBS += -L$$OUT_PWD/../lib -ldeepin-anything-server-lib

CONFIG(debug, debug|release) {
    DEPENDPATH += $$OUT_PWD/../lib
    unix:QMAKE_RPATHDIR += $$OUT_PWD/../lib
}

isEmpty(PREFIX): PREFIX = /usr

dbus.files = $$PWD/com.deepin.anything.xml
dbus.header_flags += -l LFTManager -i $$PWD/../lib/lftmanager.h
dbus.source_flags += -l LFTManager

DBUS_ADAPTORS += dbus

dbus_xmls.path = /usr/share/dbus-1/interfaces
dbus_xmls.files = $$dbus.files

dbus_service.path = /usr/share/dbus-1/system-services
dbus_service.files = $$PWD/com.deepin.anything.service

dbus_config.path = /etc/dbus-1/system.d
dbus_config.files = $$PWD/com.deepin.anything.conf

target.path = $$PREFIX/bin

INSTALLS += target dbus_xmls dbus_service dbus_config
