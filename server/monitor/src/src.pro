TARGET = deepin-anything-monitor
TEMPLATE = app
CONFIG += c++11
QT -= gui

include(../../common.pri)

SOURCES += \
    main.cpp \
    server.cpp

CONFIG(debug, debug|release) {
    QMAKE_RPATHLINKDIR += $$_PRO_FILE_PWD_/../../../library/bin/debug
} else {
    QMAKE_RPATHLINKDIR += $$_PRO_FILE_PWD_/../../../library/bin/release
}

INCLUDEPATH += ../../../kernelmod ../../lib
LIBS += -L$$OUT_PWD/../../lib -ldeepin-anything-server-lib

CONFIG(debug, debug|release) {
    DEPENDPATH += $$OUT_PWD/../../lib
    unix:QMAKE_RPATHDIR += $$OUT_PWD/../../lib
}

HEADERS += \
    server.h

isEmpty(PREFIX): PREFIX = /usr

target.path = $$PREFIX/bin

systemd_service.files = ../$${TARGET}.service
systemd_service.path = /lib/systemd/system

sysusers.files = ../systemd.sysusers.d/$${TARGET}.conf
sysusers.path = $$PREFIX/lib/sysusers.d

INSTALLS += target systemd_service sysusers
