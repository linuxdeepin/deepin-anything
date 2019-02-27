TARGET = deepin-anything-server
TEMPLATE = app
CONFIG += c++11
QT -= gui

include(../../common.pri)

SOURCES += \
    main.cpp \
    server.cpp

INCLUDEPATH += ../../../kernelmod ../lib
LIBS += -L$$OUT_PWD/../lib -ldeepin-anything-server-lib

CONFIG(debug, debug|release) {
    DEPENDPATH += $$OUT_PWD/../lib
    unix:QMAKE_RPATHDIR += $$OUT_PWD/../lib
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
