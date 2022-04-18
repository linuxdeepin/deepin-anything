TEMPLATE = lib
CONFIG += plugin
INCLUDEPATH += ../../backend/lib
DESTDIR = $$_PRO_FILE_PWD_/../handlers
QT -= gui

LIBS += -L$$OUT_PWD/../../backend/ -ldeepin-anything-backend

CONFIG(debug, debug|release) {
    DEPENDPATH += $$OUT_PWD/../../backend/lib
    unix:QMAKE_RPATHDIR += $$OUT_PWD/../../backend/lib
}

SOURCES += \
    main.cpp

OTHER_FILES += test.json
