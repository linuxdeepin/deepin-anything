CC := gcc

# Set the default target directory
RELEASE_DIR = bin/release
DEBUG_DIR = bin/debug
EXTERNAL_DIR = ../../3rdparty

ifeq ($(PROJECT_VERSION),)
    PROJECT_VERSION = 1.0.0
endif

# default install dir
ifeq ($(INSTALL_DIR),)
    INSTALL_DIR = /usr/local/lib
endif

SOURCES := \
	$(wildcard src/*.c) \
	$(wildcard src/index/*.c) \
	$(wildcard src/chinese/*.c) \
	$(wildcard $(EXTERNAL_DIR)/fsearch/*.c)

TARGET := libanything.so
SONAME = $(TARGET).0
REALNAME = $(TARGET).$(PROJECT_VERSION)

GLIB_INCL := $(shell pkg-config --cflags glib-2.0)
GLIB_LIB := $(shell pkg-config --libs glib-2.0)

INCLUDE_DIR := -Iinc -Iinc/index -I$(EXTERNAL_DIR)/fsearch ${GLIB_INCL}

CFLAGS := -std=gnu99 -Wall -Wl,--no-undefined -D_GNU_SOURCE ${INCLUDE_DIR} -shared -fPIC -fvisibility=hidden
LDFLAGS := -lpthread -lpcre ${GLIB_LIB} -shared -Wl,-soname,$(REALNAME)

all: release debug

release: $(SOURCES)
	mkdir -p $(RELEASE_DIR)
	$(CC) $(CFLAGS) -O3 $^ $(LDFLAGS) --shared -o $(RELEASE_DIR)/$(REALNAME)
	ln -sf $(REALNAME) $(RELEASE_DIR)/$(TARGET)


debug: $(SOURCES)
	mkdir -p $(DEBUG_DIR)
	$(CC) $(CFLAGS) -g -DIDX_DEBUG $^ $(LDFLAGS) --shared -o $(DEBUG_DIR)/$(REALNAME)
	ln -sf $(REALNAME) $(DEBUG_DIR)/$(TARGET)

install:
	install -m 755 $(RELEASE_DIR)/$(REALNAME) $(INSTALL_DIR)
	ln -sf $(REALNAME) $(INSTALL_DIR)/$(SONAME)
	ln -sf $(SONAME) $(INSTALL_DIR)/$(TARGET)

clean:
	rm -rf bin

.PHONY: all release debug install clean
