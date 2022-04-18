TEMPLATE = lib
CONFIG += plugin
INCLUDEPATH += ../../backend/lib
DESTDIR = $$_PRO_FILE_PWD_/../handlers
QT -= gui
QT += dbus

CONFIG(debug, debug|release) {
    QMAKE_RPATHLINKDIR += $$_PRO_FILE_PWD_/../../../../library/bin/debug
} else {
    QMAKE_RPATHLINKDIR += $$_PRO_FILE_PWD_/../../../../library/bin/release
}

LIBS += -L$$OUT_PWD/../../backend/ -ldeepin-anything-backend

CONFIG(debug, debug|release) {
    DEPENDPATH += $$OUT_PWD/../../lib
    unix:QMAKE_RPATHDIR += $$OUT_PWD/../../lib
}

SOURCES += \
    main.cpp

OTHER_FILES += update-lft.json

isEmpty(LIB_INSTALL_DIR) {
    LIB_INSTALL_DIR = $$[QT_INSTALL_LIBS]
}

PLUGINDIR = $$LIB_INSTALL_DIR/deepin-anything-backend/plugins

target.path = $$PLUGINDIR/handlers

INSTALLS += target
