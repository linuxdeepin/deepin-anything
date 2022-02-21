CC := gcc
GLIB_INCL := $(shell pkg-config --cflags glib-2.0)
GLIB_LIB := $(shell pkg-config --libs glib-2.0)
CFLAGS := -std=gnu99 -Wall -Wl,--no-undefined -D_GNU_SOURCE -Iinc -Iinc/index ${GLIB_INCL} -shared -fPIC -fvisibility=hidden
LDFLAG := -lpthread -lpcre ${GLIB_LIB}

all: release debug

release: bin/release/libanything.so

bin/release/libanything.so: src/*.c src/index/*.c src/chinese/*.c
	mkdir -p bin/release
	rm bin/release/*.so*||true
	$(CC) $(CFLAGS) -O3 -Wl,-soname,$(shell basename $@).1.0.0 $^ -o $@.1.0.0 $(LDFLAG)
	ln -s $(shell basename $@).1.0.0 $@.1.0
	ln -s $(shell basename $@).1.0.0 $@.1
	ln -s $(shell basename $@).1.0.0 $@

debug: bin/debug/libanything.so

bin/debug/libanything.so: src/*.c src/index/*.c src/chinese/*.c
	mkdir -p bin/debug
	rm bin/debug/*.so*||true
	$(CC) $(CFLAGS) -g -DIDX_DEBUG -Wl,-soname,$(shell basename $@).1.0.0 $^ -o $@.1.0.0 $(LDFLAG)
	ln -s $(shell basename $@).1.0.0 $@.1.0
	ln -s $(shell basename $@).1.0.0 $@.1
	ln -s $(shell basename $@).1.0.0 $@

clean:
	-rm -rf bin

.PHONY: all release debug clean
