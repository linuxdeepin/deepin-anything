TARGET = deepin-anything-tool
QT += core dbus
QT -= gui
TEMPLATE = app

include(../common.pri)

SOURCES += \
    main.cpp

INCLUDEPATH += ../lib
LIBS += -L$$OUT_PWD/../lib -ldeepin-anything-server-lib

CONFIG(debug, debug|release) {
    DEPENDPATH += $$OUT_PWD/../lib
    unix:QMAKE_RPATHDIR += $$OUT_PWD/../lib
}

isEmpty(PREFIX): PREFIX = /usr

target.path = $$PREFIX/bin

INSTALLS += target
