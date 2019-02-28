TEMPLATE = subdirs
SUBDIRS += monitor tool lib

monitor.depends = lib
tool.depends = lib
