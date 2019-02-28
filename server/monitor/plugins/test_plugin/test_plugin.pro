TEMPLATE = lib
CONFIG += plugin
INCLUDEPATH += ../../../lib
DESTDIR = $$_PRO_FILE_PWD_/../handlers
QT -= gui

LIBS += -L$$OUT_PWD/../../../lib -ldeepin-anything-server-lib

CONFIG(debug, debug|release) {
    DEPENDPATH += $$OUT_PWD/../../../lib
    unix:QMAKE_RPATHDIR += $$OUT_PWD/../../../lib
}

SOURCES += \
    main.cpp

OTHER_FILES += test.json
