CFLAGS := -std=gnu99 -Wall -Wl,--no-undefined -Iinc -Iinc/index -shared -fPIC -fvisibility=hidden
LDFLAG := -lpthread

all: release debug

release: bin/release/libanything.so

bin/release/libanything.so: src/*.c src/index/*.c
	mkdir -p bin/release
	rm -f $(shell dirname $@)/libanything*
	gcc ${CFLAGS} -O3 -Wl,-soname,$(shell basename $@).1.0 $^ -o $@.1.0 ${LDFLAG}
	ln -s $(shell basename $@).1.0 $@.1
	ln -s $(shell basename $@).1 $@

debug: bin/debug/libanything.so

bin/debug/libanything.so: src/*.c src/index/*.c
	mkdir -p bin/debug
	rm -f $(shell dirname $@)/libanything*
	gcc ${CFLAGS} -g -DIDX_DEBUG -Wl,-soname,$(shell basename $@).1.0 $^ -o $@.1.0 ${LDFLAG}
	ln -s $(shell basename $@).1.0 $@.1
	ln -s $(shell basename $@).1 $@

clean:
	rm -Rf bin

.PHONY: all release debug clean
